#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block_build.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"

#define VM_LOADER_CLASS_HEADER 0x04  // Wire class byte for program upload

typedef enum vm_load_state_e {
  VM_LOAD_EMPTY = 0,  // No storage; all ID lookups resolve to NULL
  VM_LOAD_OPEN  = 1,  // Storage reserved; objects, accessors, and blocks may be loaded
} vm_load_state_e;

/** @brief Quiesce execution, reset registries and arena, and set state to EMPTY. */
void vm_loader_reset(void);

/**
 * @brief Reserve arena storage and initialize registries for a new program.
 * @param obj_cnt Total object registry slots.
 * @param acc_cnt Total accessor registry slots.
 * @param blk_cnt Total block registry slots.
 * @param total_size Total arena capacity in bytes.
 */
err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint32_t total_size);

/**
 * @brief Create an object and register it at @p id.
 * @param id Object registry index [0, obj_cnt).
 * @param head Object descriptor (type, payload size, flags, name size).
 * @param name Optional tag string (NULL if head->d.name_size is 0).
 */
err_h vm_loader_add_obj(uint16_t id, const vm_obj_head_t* head, const char* name);

/**
 * @brief Load initial payload data or child pointer IDs into an object.
 * @param id Object registry ID.
 * @param start_idx Starting element offset (in elements, not bytes).
 * @param data Raw payload bytes or 2-byte little-endian child IDs (VM_OBJ_PTR).
 * @param len Data length in bytes.
 */
err_h vm_loader_set_data(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len);

/**
 * @brief Create an accessor and register it at @p acc_id.
 * @param acc_id Accessor registry index [0, acc_cnt).
 * @param root_obj_id Root object ID.
 * @param idx_count Number of path index steps.
 * @param idx_data Serialized index step payload.
 * @param idx_len Length of @p idx_data in bytes.
 */
err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len);

/**
 * @brief Create a block, resolve its wiring to pointers, and register at @p blk_id.
 * @param blk_id Block registry index [0, blk_cnt).
 * @param cfg Block configuration, pin wiring, and private state.
 */
err_h vm_loader_add_block(uint16_t blk_id, const vm_block_cfg_t* cfg);

/** @brief Current loader lifecycle state. */
vm_load_state_e vm_loader_state(void);

/** @brief Total bytes consumed in the program arena. */
uint32_t vm_loader_used(void);
