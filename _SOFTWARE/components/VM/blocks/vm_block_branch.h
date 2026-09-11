#pragma once
#include "esp_compiler.h"
#include "vm_block_support.h"

#define VM_BRANCH_CUSTOM_LEN 0u
#define VM_BRANCH_NONE 0xFFu

/*
Flow routers -- VM_BLK_IF (two-way) and VM_BLK_SWITCH (up to 16-way).
Enable-driven and stateless (custom_len = 0). Outputs act as enable gates for child blocks.
*/

static inline void vm_branch_drive(vm_block_h b, uint8_t taken) {
  for (uint8_t pin = 0; pin < b->cfg.q_cnt; ++pin) {
    vm_block_drive_gate(b, pin, pin == taken);
  }
}

static inline bool vm_verify_if(vm_block_h b) {
  return vm_block_require(b, 1, 2, 0x1u);
}

static inline bool vm_verify_switch(vm_block_h b) {
  return vm_block_require(b, 1, 1, 0x1u);
}

static inline const vm_accessor_t* vm_branch_selector(vm_block_h b, uint8_t min_q) {
  return vm_block_require(b, 1, min_q, 0x1u) ? vm_block_get_inputs(b)[0] : NULL;
}

static inline void vm_blk_if(vm_block_h b) {
  const vm_accessor_t* in0 = vm_branch_selector(b, 2);
  uint8_t taken = VM_BRANCH_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      bool cond = false;
      if (vm_block_read_bool(&cond, b, in0)) taken = cond ? 0u : 1u;
    }
  }

  vm_branch_drive(b, taken);
  vm_block_set_eno(b, (taken != VM_BRANCH_NONE) && !g_vm_block_fault);
}

static inline void vm_blk_switch(vm_block_h b) {
  const vm_accessor_t* in0 = vm_branch_selector(b, 1);
  uint8_t taken = VM_BRANCH_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      int32_t sel = 0;
      if (vm_block_check(b, VM_OBJ_GET_VAL(sel, in0)) && sel >= 0 && sel < (int32_t)b->cfg.q_cnt) {
        taken = (uint8_t)sel;
      }
    }
  }

  vm_branch_drive(b, taken);
  vm_block_set_eno(b, (taken != VM_BRANCH_NONE) && !g_vm_block_fault);
}
