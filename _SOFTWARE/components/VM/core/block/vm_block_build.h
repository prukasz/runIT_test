#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block.h"

/*
Block construction staging descriptor:
Resolves numeric wiring IDs from packet to direct RAM pointers before arena allocation.
*/

typedef struct vm_block_cfg_t {
  uint16_t block_idx;   // Visual block identifier
  uint8_t  block_type;  // Palette entry index
  uint8_t  in_cnt;      // <= VM_BLOCK_MAX_IN
  uint8_t  q_cnt;       // <= VM_BLOCK_MAX_OUT
  uint8_t  en_cnt;      // <= VM_BLOCK_MAX_EN (0 = root, always enabled)
  uint8_t  en_mode;     // VM_BLK_EN_ANY / _ALL
  uint8_t  on_error;    // VM_BLK_ERR_STOP / _CONTINUE
  uint16_t custom_len;  // Private state length (zeroed on creation)

  const uint16_t* in_acc_ids;   // in_cnt accessor IDs (VM_BLOCK_NO_ID for unwired pin)
  const uint16_t* out_obj_ids;  // q_cnt object IDs
  const uint16_t* en_acc_ids;   // en_cnt accessor IDs
  uint16_t        eno_obj_id;   // ENO object ID (VM_BLOCK_NO_ID if none)
  const void*     custom_data;  // Initial custom payload (copied if non-NULL)
} vm_block_cfg_t;

/** @brief Sentinel for unwired pin or absent ENO. */
#define VM_BLOCK_NO_ID 0xFFFFu

/**
 * @brief Allocates one block in arena, resolves all ID references to pointers, and binds to registry.
 * @param out Receives the allocated block handle (NULL on failure).
 * @param id Registry ID to bind (or VM_ID_NONE).
 * @param cfg Configuration and wiring ID descriptor.
 */
err_h vm_block_create(vm_block_h* out, uint16_t id, const vm_block_cfg_t* cfg);

