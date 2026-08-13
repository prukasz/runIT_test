#pragma once
/**
 * @file dec_vm_loader.h
 * @brief Header-only decoder table for the "VM Program Load" packet class (0x04).
 *
 * Wire format handled by this class:
 * @code
 *   [0x04] [0xYY] [ payload ]
 *    class  packet
 * @endcode
 *
 * Unlike [[dec_sys_contracts.h]] these packets are not fixed-size structs --
 * 0x42 and 0x43 carry a repeating record list whose entries vary in length --
 * so they are walked with an explicit cursor instead of the
 * convert_to_packet() copy. Every length is checked against the bytes
 * actually present before it is used; a truncated frame raises
 * ERR_VM_LOAD_SHORT_RECORD rather than reading past the buffer.
 *
 * Load sequence (see vm_loader.h for the state rules):
 * @code
 *   0x40 reset      : -
 *   0x41 open       : u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u32 total_size
 *   0x42 add objs   : u8 n, n x { u16 id, vm_obj_head_t head,
 *                                 char name[head.d.name_size] }
 *   0x43 set data   : u8 n, n x { u16 id, u16 start_idx, u16 byte_len,
 *                                 u8 data[byte_len] }
 *   0x44 add acc    : u8 n, n x { u16 acc_id, u16 root_obj_id, u8 idx_count,
 *                                 u8 idx_len, u8 idx_data[idx_len] }
 *                     idx_data is idx_count records of { u8 kind, payload }:
 *                       LITERAL -> u32 position
 *                       REF     -> u16 accessor id (must already exist)
 *                       NAME    -> u8 len, char name[len]
 *   0x45 add block  : u16 blk_id, u16 block_idx,
 *                     u8 block_type, u8 in_cnt, u8 q_cnt, u8 en_cnt, u8 en_mode,
 *                     u8 on_error, u16 custom_len, u16 eno_obj_id,
 *                     in_cnt x u16 accessor id,
 *                     q_cnt  x u16 object id,
 *                     en_cnt x u16 accessor id,
 *                     custom_len x u8 initial private state
 * @endcode
 *
 * 0x45 carries exactly one block, unlike the batching packets above: a block
 * record varies in length three separate ways (inputs, outputs, private
 * state), so batching would mean a length prefix per record and a cursor that
 * can desynchronise. One block per frame keeps the framing trivial and puts a
 * malformed record's blast radius at that block.
 *
 * VM_BLOCK_NO_ID (0xFFFF) spells "absent" in two places: an *input*, where the
 * pin exists but nothing is wired to it and the block falls back to its own
 * constant, and ENO, where the block publishes nothing. It is not legal for an
 * output, nor inside the enable list -- a listed enable that fails to resolve is
 * a malformed program. An absent enable has no spelling of its own: it is
 * en_cnt == 0, a root that always runs.
 *
 * en_cnt > 1 makes the flow graph a DAG rather than a tree, and en_mode says how
 * the sources combine: VM_BLK_EN_ANY for branches rejoining ("either path
 * reached me"), VM_BLK_EN_ALL for independent conditions that must all hold.
 * There is no section id here -- section membership is the section packet's
 * ordered id list, not a field on the block. See [[VM_EXEC.MD]].
 *
 * All multi-byte fields are little-endian, matching the target.
 *
 * `start_idx` is present unconditionally rather than only on multi-element
 * writes: making its presence depend on comparing byte_len against the
 * object's element width would leave the framing dependent on a lookup the
 * parser has to trust, and would make a single non-zero element impossible
 * to address (one element is never "larger than one element").
 *
 * `payload_size` is a **byte** count, not an element count. The client owns
 * sizing, so it owns the type-width table too -- and in exchange the device
 * does no arithmetic on sizes at all, which removes the overflow that
 * multiplying a 16-bit count by a width and truncating back into 16 bits used
 * to risk. A size that is not a whole number of elements is rejected by
 * vm_obj_shape() (ERR_VM_OBJ_BAD_SIZE); a wrong-but-aligned one is contained,
 * since vm_obj_elem_ptr() bounds every access against it.
 *
 * For a VM_OBJ_PTR object the 0x43 data field is a list of u16 child ids, two
 * bytes each; children must already have been created by an earlier 0x42.
 */

#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "sys_error.h"
#include "vm_loader.h"

#undef OWNER
#define OWNER OWNER_DEC_VM_LOADER

/* VM_LOADER_CLASS_HEADER (0x04) is defined in vm_loader.h, not here: the
   class is 1:1 with the VM component, so the constant lives with that
   component's own public contract -- same split as SYS_ACTIONS_CLASS_HEADER,
   and unlike SYS_CONTRACTS_CLASS_HEADER which spans three components and is
   therefore owned by its codec. */

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_VM_LOADER_TAG "dec_vm_loader"

#define HEADER_packet_vm_reset 0x40
#define HEADER_packet_vm_open 0x41
#define HEADER_packet_vm_add_objs 0x42
#define HEADER_packet_vm_set_data 0x43
#define HEADER_packet_vm_add_acc 0x44
#define HEADER_packet_vm_add_block 0x45

// little-endian readers -- the cursor is a raw byte stream, so nothing here
// may assume the alignment a struct cast would imply
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
    SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = pkt, .need = (uint16_t)need,
               .got = (uint16_t)(len > off ? len - off : 0));
  }
  return NULL;
}

/** @brief 0x40 -- drop whatever is loaded, leaving the VM fail-closed. */
static inline err_h decoder_packet_vm_reset(void) {
  vm_loader_reset();
  ESP_LOGI(DEC_VM_LOADER_TAG, "storage reset");
  return NULL;
}

/** @brief 0x41 -- reserve the id tables and cap the arena. */
static inline err_h decoder_packet_vm_open(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_open, 0, len, 10));
  uint16_t obj_cnt = dec_vm_u16(body);
  uint16_t acc_cnt = dec_vm_u16(body + 2);
  uint16_t blk_cnt = dec_vm_u16(body + 4);
  uint32_t total = dec_vm_u32(body + 6);
  SE_RET_IF_ERR(vm_loader_open(obj_cnt, acc_cnt, blk_cnt, total));
  ESP_LOGI(DEC_VM_LOADER_TAG, "open: %u objects, %u accessors, %u blocks, %lu bytes", obj_cnt, acc_cnt, blk_cnt, (unsigned long)total);
  return NULL;
}

/** @brief 0x45 -- create one block and resolve its wiring. */
static inline err_h decoder_packet_vm_add_block(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_block, 0, len, 14));
  uint16_t blk_id = dec_vm_u16(body);
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
    vm_block_h blk = vm_block_by_id(blk_id);
    memcpy(vm_block_custom_data(blk), body + off, cfg.custom_len);
  }
  return NULL;
}

/** @brief 0x44 -- create a batch of accessors. */
static inline err_h decoder_packet_vm_add_acc(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;

  for (uint8_t i = 0; i < n; i++) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, off, len, 6));
    uint16_t acc_id = dec_vm_u16(body + off);
    uint16_t root_id = dec_vm_u16(body + off + 2);
    uint8_t idx_count = body[off + 4];
    uint8_t idx_len = body[off + 5];
    off += 6;

    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_acc, off, len, idx_len));
    SE_RET_IF_ERR(vm_loader_add_accessor(acc_id, root_id, idx_count, body + off, idx_len));
    off += idx_len;
  }
  return NULL;
}

/** @brief 0x42 -- create a batch of objects. */
static inline err_h decoder_packet_vm_add_objs(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_add_objs, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;

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

/** @brief 0x43 -- fill payload bytes, or link children for a PTR object. */
static inline err_h decoder_packet_vm_set_data(const uint8_t* body, size_t len) {
  SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;

  for (uint8_t i = 0; i < n; i++) {
    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, off, len, 6));
    uint16_t id = dec_vm_u16(body + off);
    uint16_t start_idx = dec_vm_u16(body + off + 2);
    uint16_t byte_len = dec_vm_u16(body + off + 4);
    off += 6;

    SE_RET_IF_ERR(dec_vm_need(HEADER_packet_vm_set_data, off, len, byte_len));
    SE_RET_IF_ERR(vm_loader_set_data(id, start_idx, body + off, byte_len));
    off += byte_len;
  }
  return NULL;
}

/**
 * @brief Class handler for VM_LOADER_CLASS_HEADER (0x04).
 *
 * @param data Frame bytes with the class byte already stripped -- data[0] is 0xYY.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for an unmapped
 *               header, or the loader's own error chain.
 */
static inline err_h dec_vm_loader_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t* body = data + 1;
  size_t body_len = len - 1;

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
    default:
      ESP_LOGW(DEC_VM_LOADER_TAG, "unknown packet header 0x%02X", data[0]);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = VM_LOADER_CLASS_HEADER, .packet_header = data[0]);
  }
}
