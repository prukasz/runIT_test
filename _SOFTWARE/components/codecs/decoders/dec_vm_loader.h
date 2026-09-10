#pragma once
/**
 * @file dec_vm_loader.h
 * @brief Header-only decoder table for the "VM Program Load" packet class (0x04).
 *
 * Wire format handled by this class:
 * @code
 *   [0x04] [0xYY] [ payload... ]
 *    class  packet
 * @endcode
 *
 * Architecture & framing rules:
 * - **Unfixed Payload Walking**: Records vary in length; parsed using explicit cursor
 *   bounds checks (`dec_vm_need`) rather than fixed struct casts.
 * - **Strict Bounds Checking**: Truncated frames emit `ERR_VM_LOAD_SHORT_RECORD`.
 * - **Little-Endian**: Multi-byte integers decoded explicitly via `dec_vm_u16`/`dec_vm_u32`.
 * - **Topological Execution**: Block frames (`0x45`) are uploaded in execution order.
 */

#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "sys_error.h"
#include "utils.h"
#include "vm_exec.h"
#include "vm_loader.h"
#include "vm_override.h"
#include "vm_sub.h"

#undef OWNER
#define OWNER OWNER_DEC_VM_LOADER

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_VM_LOADER_TAG "dec_vm_loader"

#define HEADER_packet_vm_reset     0x40
#define HEADER_packet_vm_open      0x41
#define HEADER_packet_vm_add_objs  0x42
#define HEADER_packet_vm_set_data  0x43
#define HEADER_packet_vm_add_acc   0x44
#define HEADER_packet_vm_add_block 0x45
#define HEADER_packet_vm_subscribe 0x47
#define HEADER_packet_vm_exec      0x48

/** Single execution-control payload layout: 04 48 <command>. */
typedef struct {
  uint8_t command;
} packet_vm_exec_t;

/* ========================================================================= */
/* Cursor & Little-Endian Stream Helpers                                     */
/* ========================================================================= */

static inline uint16_t dec_vm_u16(const uint8_t* p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t dec_vm_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void dec_vm_u16_array(uint16_t* dst, const uint8_t* src, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) dst[i] = dec_vm_u16(src + (size_t)i * 2);
}

static inline err_h dec_vm_need(uint8_t pkt, size_t off, size_t len, size_t need) {
  if (unlikely((uint32_t)off + (uint32_t)need > (uint32_t)len)) {
    SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = pkt, .need = (uint16_t)need, .got = (uint16_t)(len > off ? len - off : 0));
  }
  return NULL;
}

/* ========================================================================= */
/* Packet Decoder Functions                                                  */
/* ========================================================================= */

/**
 * @brief Packet 0x40: VM Reset
 *
 * - **Wire Layout**: None (0 payload bytes).
 * - **Action**:
 *   - Enforces lifecycle barrier: stops pass admission and waits for active pass to finish.
 *   - Reclaims dynamic heap objects, clears registries, and resets bump arena.
 *   - Leaves execution stopped in fail-closed state (`VM_LOAD_IDLE`).
 */
static inline err_h decoder_packet_vm_reset(void) {
  vm_loader_reset();
  DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "storage reset"););
  return NULL;
}

/**
 * @brief Packet 0x41: VM Program Open
 *
 * - **Wire Layout** (10 bytes total):
 *   - `u16 obj_cnt`    : Number of object slots to allocate in `VM_REG_OBJ`
 *   - `u16 acc_cnt`    : Number of accessor slots to allocate in `VM_REG_ACC`
 *   - `u16 blk_cnt`    : Number of block slots to allocate in `VM_REG_BLK`
 *   - `u32 total_size` : Total memory footprint in bytes to allocate for bump arena
 * - **Action**:
 *   - Validates memory availability against DRAM limits before modifying active state.
 *   - Tears down prior program and re-arms registries and arena at declared capacity.
 *   - Transitions loader state to `VM_LOAD_OPEN`.
 */
static inline err_h decoder_packet_vm_open(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_open, 0, len, 10));
  uint16_t obj_cnt = dec_vm_u16(body);
  uint16_t acc_cnt = dec_vm_u16(body + 2);
  uint16_t blk_cnt = dec_vm_u16(body + 4);
  uint32_t total = dec_vm_u32(body + 6);
  SE_RET_IF_ERR(vm_loader_open(obj_cnt, acc_cnt, blk_cnt, total));
  DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "open: %u objects, %u accessors, %u blocks, %lu bytes", obj_cnt, acc_cnt, blk_cnt, (unsigned long)total););
  return NULL;
}

/**
 * @brief Packet 0x42: Add Objects Batch
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of object records in frame
 *   - `n ×` records:
 *     - `u16 id`                      : Target object ID in `VM_REG_OBJ`
 *     - `vm_obj_head_t head` (4 bytes): Packed header (`payload_size`, `obj_t`, `name_size`, flags)
 *     - `char name[head.d.name_size]` : Optional tag identifier string (<= 15 bytes, unterminated)
 * - **Action**:
 *   - Carves 4-byte aligned chunk in bump arena, zeroes payload memory, and stores header.
 *   - Binds object pointer into registry index `id`.
 *   - Appends tag name to object tail.
 */
static inline err_h decoder_packet_vm_add_objs(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_objs, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_objs, off, len, 2 + sizeof(vm_obj_head_t)));
    uint16_t id = dec_vm_u16(body + off);

    vm_obj_head_t head;
    memcpy(&head, body + off + 2, sizeof(head));
    off += 2 + sizeof(head);

    uint8_t name_len = head.d.name_size;
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_objs, off, len, name_len));
    const char* name = name_len ? (const char*)(body + off) : NULL;
    off += name_len;

    SE_RET_IF_ERR(vm_loader_add_obj(id, &head, name));
  }
  return NULL;
}

/**
 * @brief Packet 0x43: Set Object Data / Runtime Override
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of data chunk records in frame
 *   - `n ×` records:
 *     - `u16 id`             : Target object ID in `VM_REG_OBJ`
 *     - `u16 start_idx`      : Element start index (or byte offset)
 *     - `u16 byte_len`       : Number of payload bytes
 *     - `u8 data[byte_len]`  : Raw data bytes, or child `u16` IDs for `VM_OBJ_PTR`
 * - **Action**:
 *   - When VM is stopped (`VM_RUN_STOPPED`): Writes bytes directly via `vm_loader_set_data()`.
 *     For pointer containers, validates child IDs and links children in arena.
 *   - When VM is running: Enqueues variable update via `vm_override_post()`, applied
 *     atomically at supervisor cycle drain.
 */
static inline err_h decoder_packet_vm_set_data(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, off, len, 6));
    uint16_t id = dec_vm_u16(body + off);
    uint16_t start_idx = dec_vm_u16(body + off + 2);
    uint16_t byte_len = dec_vm_u16(body + off + 4);
    off += 6;

    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, off, len, byte_len));
    if (vm_exec_mode() != VM_RUN_STOPPED) {
      SE_RET_IF_ERR(vm_override_post(id, start_idx, body + off, byte_len));
    } else {
      SE_RET_IF_ERR(vm_loader_set_data(id, start_idx, body + off, byte_len));
    }
    off += byte_len;
  }
  return NULL;
}

/**
 * @brief Packet 0x44: Add Accessors Batch
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of accessor records in frame
 *   - `n ×` records:
 *     - `u16 acc_id`          : Accessor registry ID in `VM_REG_ACC`
 *     - `u16 root_obj_id`     : Root object ID in `VM_REG_OBJ`
 *     - `u8 idx_count`        : Number of chained index steps
 *     - `u8 idx_len`          : Total length in bytes of encoded index chain
 *     - `u8 idx_data[idx_len]`: Sequence of `{ u8 kind, payload }`:
 *       - `VM_IDX_LITERAL`: `u32` constant position
 *       - `VM_IDX_REF`    : `u16` accessor ID (must already exist)
 *       - `VM_IDX_NAME`   : `u8 len`, `char name[len]`
 * - **Action**:
 *   - Allocates accessor descriptor and trailing index array in bump arena.
 *   - Binds descriptor to registry index `acc_id`.
 *   - Builds resolution cache (`vm_accessor_cache_build`) for static literal paths.
 */
static inline err_h decoder_packet_vm_add_acc(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, off, len, 6));
    uint16_t acc_id = dec_vm_u16(body + off);
    uint16_t root_id = dec_vm_u16(body + off + 2);
    uint8_t  idx_count = body[off + 4];
    uint8_t  idx_len = body[off + 5];
    off += 6;

    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, off, len, idx_len));
    SE_RET_IF_ERR(vm_loader_add_accessor(acc_id, root_id, idx_count, body + off, idx_len));
    off += idx_len;
  }
  return NULL;
}

/**
 * @brief Packet 0x45: Add Block (Single Block per Frame)
 *
 * - **Wire Layout**:
 *   - Fixed Header (14 bytes):
 *     - `u16 blk_id`    : Block registry ID in `VM_REG_BLK`
 *     - `u16 block_idx` : Sequential execution order index
 *     - `u8  block_type`: Palette block type ID (must be registered in palette table)
 *     - `u8  in_cnt`    : Number of input accessor IDs
 *     - `u8  q_cnt`     : Number of output object IDs
 *     - `u8  en_cnt`    : Number of enable accessor IDs (`0` = always enabled)
 *     - `u8  en_mode`   : Enable combination logic (`VM_BLK_EN_ANY` or `VM_BLK_EN_ALL`)
 *     - `u8  on_error`  : Error policy (`VM_BLK_ERR_STOP` or `VM_BLK_ERR_CONT`)
 *     - `u16 custom_len`: Length of initial private state bytes
 *     - `u16 eno_obj_id`: Output ENO status object ID (`VM_BLOCK_NO_ID` if unmapped)
 *   - Trailing Arrays:
 *     - `in_cnt  × u16` : Input accessor IDs (`VM_BLOCK_NO_ID` for unwired pins)
 *     - `q_cnt   × u16` : Output object IDs (automatically marked `usr_protected`)
 *     - `en_cnt  × u16` : Enable accessor IDs
 *     - `custom_len × u8`: Initial private configuration / bytecode / constant data
 * - **Action**:
 *   - Validates pin counts (`VM_BLOCK_MAX_IN`, `VM_BLOCK_MAX_OUT`, `VM_BLOCK_MAX_EN`).
 *   - Allocates block struct in bump arena, binds registry ID, and wires pins.
 *   - Copies private state into block custom data memory.
 */
static inline err_h decoder_packet_vm_add_block(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, 0, len, 14));
  uint16_t       blk_id = dec_vm_u16(body);
  vm_block_cfg_t cfg = {
      .block_idx = dec_vm_u16(body + 2),
      .block_type = body[4],
      .in_cnt = body[5],
      .q_cnt = body[6],
      .en_cnt = body[7],
      .en_mode = body[8],
      .on_error = body[9],
      .custom_len = dec_vm_u16(body + 10),
      .eno_obj_id = dec_vm_u16(body + 12),
  };
  size_t off = 14;

  if (cfg.in_cnt > VM_BLOCK_MAX_IN || cfg.q_cnt > VM_BLOCK_MAX_OUT || cfg.en_cnt > VM_BLOCK_MAX_EN) {
    SE_RET_ERR(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg.block_idx, .in_cnt = cfg.in_cnt, .q_cnt = cfg.q_cnt);
  }

  uint16_t in_ids[VM_BLOCK_MAX_IN];
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.in_cnt * 2));
  dec_vm_u16_array(in_ids, body + off, cfg.in_cnt);
  off += (size_t)cfg.in_cnt * 2;
  cfg.in_acc_ids = cfg.in_cnt ? in_ids : NULL;

  uint16_t out_ids[VM_BLOCK_MAX_OUT];
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.q_cnt * 2));
  dec_vm_u16_array(out_ids, body + off, cfg.q_cnt);
  off += (size_t)cfg.q_cnt * 2;
  cfg.out_obj_ids = cfg.q_cnt ? out_ids : NULL;

  uint16_t en_ids[VM_BLOCK_MAX_EN];
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.en_cnt * 2));
  dec_vm_u16_array(en_ids, body + off, cfg.en_cnt);
  off += (size_t)cfg.en_cnt * 2;
  cfg.en_acc_ids = cfg.en_cnt ? en_ids : NULL;

  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, off, len, cfg.custom_len));
  SE_RET_IF_ERR(vm_loader_add_block(blk_id, &cfg));

  if (cfg.custom_len) {
    vm_block_h blk = vm_block_get_by_id(blk_id);
    memcpy(vm_block_get_custom_data(blk), body + off, cfg.custom_len);
  }
  return NULL;
}

/**
 * @brief Packet 0x47: Subscribe Object Telemetry
 *
 * - **Wire Layout**:
 *   - `u8 count`: Number of object IDs in frame (`0` clears all subscriptions)
 *   - `count × u16`: Object registry IDs to stream
 * - **Action**:
 *   - Registers object IDs for cyclical freshness checks.
 *   - Emits telemetry notifications whenever marked fresh (`f.upd = 1`) at pass end.
 */
static inline err_h decoder_packet_vm_subscribe(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_subscribe, 0, len, 1));
  uint8_t n = body[0];
  if (n > 0) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_subscribe, 1, len, (size_t)n * 2u));
  }
  return vm_sub_handle_packet(body, len);
}

/**
 * @brief Packet 0x48: Execution Control
 *
 * - **Wire Layout** (1 byte):
 *   - `u8 command`: `vm_exec_command_e` enum value:
 *     - `VM_EXEC_START` (0x01): Starts supervisor task / unpauses execution
 *     - `VM_EXEC_STOP`  (0x02): Stops cyclic supervisor execution
 *     - `VM_EXEC_STEP`  (0x03): Executes exactly one scan pass, then stops
 *     - `VM_EXEC_RESET` (0x04): Halts execution and invokes full loader reset
 * - **Action**:
 *   - Routes command directly to `vm_exec_control()`, or performs full `vm_loader_reset()`.
 */
static inline err_h decoder_packet_vm_exec(const uint8_t* body, size_t len) {
  if (len != sizeof(packet_vm_exec_t)) {
    SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = HEADER_packet_vm_exec, .need = sizeof(packet_vm_exec_t), .got = (uint16_t)len);
  }
  uint8_t cmd = body[0];
  if (cmd == VM_EXEC_RESET || cmd == 0x04) {
    vm_loader_reset();
    DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "vm execution reset"););
    return NULL;
  }
  if (cmd == 0x02) {
    vm_exec_stop();
    DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "vm execution stopped (VM_EXEC_STOP)"););
    return NULL;
  }
  if (cmd == 0x01 || cmd == VM_EXEC_NORMAL_MODE) {
    vm_exec_set_mode(VM_RUN_RUNNING);
    DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "vm execution started (VM_RUN_RUNNING)"););
    return NULL;
  }
  return vm_exec_control((vm_exec_command_e)cmd);
}

/* ========================================================================= */
/* Class Dispatcher Function                                                 */
/* ========================================================================= */

/**
 * @brief Class handler for VM_LOADER_CLASS_HEADER (0x04).
 *
 * @param data Frame bytes with class byte stripped (data[0] is packet byte 0xYY).
 * @param len Total number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for unknown header,
 *               or the decoder's returned error chain.
 */
static inline err_h dec_vm_loader_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t* body = data + 1;
  size_t         body_len = len - 1;

  switch (data[0]) {
    case HEADER_packet_vm_reset:
      return decoder_packet_vm_reset();
    case HEADER_packet_vm_open:
      return decoder_packet_vm_open(body, body_len);
    case HEADER_packet_vm_add_objs:
      return decoder_packet_vm_add_objs(body, body_len);
    case HEADER_packet_vm_set_data:
      return decoder_packet_vm_set_data(body, body_len);
    case HEADER_packet_vm_add_acc:
      return decoder_packet_vm_add_acc(body, body_len);
    case HEADER_packet_vm_add_block:
      return decoder_packet_vm_add_block(body, body_len);
    case HEADER_packet_vm_subscribe:
      return decoder_packet_vm_subscribe(body, body_len);
    case HEADER_packet_vm_exec:
      return decoder_packet_vm_exec(body, body_len);
    default:
      ESP_LOGW(DEC_VM_LOADER_TAG, "unknown packet header 0x%02X", data[0]);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = VM_LOADER_CLASS_HEADER, .packet_header = data[0]);
  }
}
