#pragma once
#include "esp_compiler.h"
#include "vm_block.h"

#define VM_BRANCH_CUSTOM_LEN 0u
#define BR_NONE 0xFFu

/*
Flow routers -- VM_BLK_IF (two-way) and VM_BLK_SWITCH (up to 16-way).
Enable-driven and stateless (custom_len = 0). Outputs act as enable gates for child blocks.
*/

static inline void vm_branch_drive(vm_block_h b, uint8_t taken) {
  vm_obj_h* q = vm_block_outputs(b);
  const uint8_t n = b->cfg.q_cnt;
  for (uint8_t i = 0; i < n; i++) {
    if (i == taken) {
      uint8_t one = 1;
      BLOCK_CALL(VM_OBJ_SET_VAL_AT(one, q[i], 0), b);
    } else {
      vm_obj_clear_quiet(q[i]);
    }
  }
}

static inline const vm_accessor_t* vm_branch_selector(vm_block_h b, uint8_t min_q) {
  const bool shape = (b->cfg.in_cnt >= 1) && (b->cfg.q_cnt >= min_q);
  const vm_accessor_t* in0 = shape ? vm_block_inputs(b)[0] : NULL;
  if (likely(in0 != NULL)) return in0;

  g_vm_block_fault = true;
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return NULL;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;

  err_h e = shape ? vm_block_err_pin_unlinked(b->cfg.block_idx, 0, false)
                  : VM_BLK_ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt,
                                   .q_cnt = b->cfg.q_cnt);
  vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  return NULL;
}

static inline void vm_blk_if(vm_block_h b) {
  const vm_accessor_t* in0 = vm_branch_selector(b, 2);
  uint8_t taken = BR_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      float cond = 0.0f;
      err_h e = VM_OBJ_GET_VAL(cond, in0);
      if (unlikely(e)) {
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      } else {
        taken = (cond != 0.0f) ? 0u : 1u;
      }
    }
  }

  vm_branch_drive(b, taken);
  vm_block_set_ENO(b, (taken != BR_NONE) && !g_vm_block_fault);
}

static inline void vm_blk_switch(vm_block_h b) {
  const vm_accessor_t* in0 = vm_branch_selector(b, 1);
  uint8_t taken = BR_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      int32_t sel = 0;
      err_h e = VM_OBJ_GET_VAL(sel, in0);
      if (unlikely(e)) {
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      } else if (likely(sel >= 0 && sel < (int32_t)b->cfg.q_cnt)) {
        taken = (uint8_t)sel;
      }
    }
  }

  vm_branch_drive(b, taken);
  vm_block_set_ENO(b, (taken != BR_NONE) && !g_vm_block_fault);
}
