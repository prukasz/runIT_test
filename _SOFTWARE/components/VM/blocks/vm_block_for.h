#pragma once
#include <math.h>
#include "esp_compiler.h"
#include "vm_block_support.h"
#include "vm_exec.h"

/*
VM_BLK_FOR -- C-style float iterator loop with span claiming.
Layout in custom_data:
  [0..3]   vm_span_t span       The range this block owns (MUST be first)
  [4..7]   f32       k_start    Fallback when input 0 is unwired
  [8..11]  f32       k_end      Fallback when input 1 is unwired
  [12..15] f32       k_step     Fallback when input 2 is unwired
  [16..17] u16       max_turns  Hard turn budget to bound execution
  [18]     u8        op         VM_FOR_OP_*  (advance iterator)
  [19]     u8        cmp        VM_FOR_CMP_* (loop condition)
  [20]     u8        rt         Runtime latch (VM_FOR_RT_*; 0 on wire)
  [21..23] u8[3]     pad
*/

typedef enum vm_for_op_e {
  VM_FOR_OP_ADD = 0,
  VM_FOR_OP_SUB,
  VM_FOR_OP_MUL,
  VM_FOR_OP_DIV,
  VM_FOR_OP_CNT
} vm_for_op_e;

typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,
  VM_FOR_CMP_LE,
  VM_FOR_CMP_GT,
  VM_FOR_CMP_GE,
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

typedef struct vm_for_code_t {
  vm_span_t span;
  float k_start;
  float k_end;
  float k_step;
  uint16_t max_turns;
  uint8_t op;
  uint8_t cmp;
  uint8_t rt;
  uint8_t _pad[3];
} vm_for_code_t;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");

#define VM_FOR_IN_START 0u
#define VM_FOR_IN_END 1u
#define VM_FOR_IN_STEP 2u

#define VM_FOR_RT_BAD 0x01u
#define VM_FOR_BAD_CAPPED 0u
#define VM_FOR_BAD_NOT_FINITE 1u

static inline void vm_for_bad_loop(vm_block_h b, vm_for_code_t* c, uint32_t turns, uint8_t reason) {
  g_vm_block_fault = true;
  if (c->rt & VM_FOR_RT_BAD) return;
  c->rt |= VM_FOR_RT_BAD;
  vm_block_report_error(VM_BLK_ERR_NEW(ERR_VM_FOR_BAD_LOOP, .block_idx = b->cfg.block_idx, .turns = turns, .cap = c->max_turns,
                  .reason = reason), b->cfg.block_idx, b->cfg.block_type);
}

static inline bool vm_for_keep_going(uint8_t cmp, float i, float end) {
  switch (cmp) {
    case VM_FOR_CMP_LT: return i < end;
    case VM_FOR_CMP_LE: return i <= end;
    case VM_FOR_CMP_GT: return i > end;
    default: return i >= end;
  }
}

static inline void vm_blk_for(vm_block_h b) {
  const vm_span_t* sp = vm_block_get_span(b);
  const vm_span_t range = sp ? *sp : (vm_span_t){0, 0};
  vm_block_claim_span(b, range.start, range.end);

  const bool owned = (b->cfg.rt & VM_BLK_RT_SPAN) != 0;

  if (unlikely(b->cfg.custom_len < sizeof(vm_for_code_t))) {
    vm_block_cfg_bad(b);
    vm_block_set_eno(b, false);
    return;
  }
  vm_for_code_t* c = (vm_for_code_t*)vm_block_get_custom_data(b);
  if (unlikely(c->op >= VM_FOR_OP_CNT || c->cmp >= VM_FOR_CMP_CNT)) {
    vm_block_cfg_bad(b);
    vm_block_set_eno(b, false);
    return;
  }

  float i = 0.0f, end = 0.0f, step = 0.0f;
  bool go = owned;
  IF_BLOCK_ENABLED(b) {
    go = go && vm_block_param_f32(&i, b, VM_FOR_IN_START, c->k_start);
    go = go && vm_block_param_f32(&end, b, VM_FOR_IN_END, c->k_end);
    go = go && vm_block_param_f32(&step, b, VM_FOR_IN_STEP, c->k_step);
    go = go && isfinite(i) && isfinite(end) && isfinite(step);
  } else {
    go = false;
  }

  if (!go || !vm_for_keep_going(c->cmp, i, end)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_set_eno(b, true);

  vm_obj_h idx = (b->cfg.q_cnt >= 1) ? vm_block_get_outputs(b)[0] : NULL;
  const uint32_t budget = c->max_turns;
  uint32_t turns = 0;

  while (vm_for_keep_going(c->cmp, i, end)) {
    if (unlikely(turns >= budget)) {
      vm_for_bad_loop(b, c, turns, VM_FOR_BAD_CAPPED);
      return;
    }

    if (idx) {
      err_h e = VM_OBJ_SET_VAL_AT(i, idx, 0);
      if (unlikely(e)) {
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
        return;
      }
    }

    vm_exec_run_range(range.start, range.end);
    if (vm_exec_cancelled()) return;
    turns++;

    switch (c->op) {
      case VM_FOR_OP_ADD: i += step; break;
      case VM_FOR_OP_SUB: i -= step; break;
      case VM_FOR_OP_MUL: i *= step; break;
      default:            i /= step; break;
    }
    if (unlikely(!isfinite(i))) {
      vm_for_bad_loop(b, c, turns, VM_FOR_BAD_NOT_FINITE);
      return;
    }
  }

  c->rt &= (uint8_t)~VM_FOR_RT_BAD;
}
