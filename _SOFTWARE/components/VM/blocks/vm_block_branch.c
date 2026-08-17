#include "vm_block_branch.h"
#include "esp_compiler.h"

#define OWNER OWNER_VM_BLOCK

/*
The two flow routers -- see vm_block_branch.h for why they are enable-driven and
why they retract their own outputs. What is left here is small enough to say in
one place: check the shape, read the pin, drive the one-hot, assert the flow.
*/

/** @brief "No branch" for drive(): every output clears and none is asserted.
 *  Outside the 0..15 an output index can occupy, so it can never collide with
 *  a real selection. */
#define BR_NONE 0xFFu

/**
 * @brief Drive the one-hot: `taken` gets a loud 1, every other output a quiet 0.
 *
 * The asymmetry is vm_block_set_ENO()'s, for vm_block_set_ENO()'s reason, and it
 * is the whole of what makes a branch output usable as an enable source: the
 * assertion is news, the withdrawal is not.
 *
 * A NULL output is unreachable for an uploaded program -- the loader rejects a
 * NO_ID output pin -- but a hand-built fixture can produce one, so the write
 * path reports it (VM_OBJ_SET_VAL_AT faults on a null object) and the clear
 * path tolerates it (vm_obj_clear_quiet() returns on NULL).
 */
static void drive(vm_block_h b, uint8_t taken) {
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

/**
 * @brief The shape checks and the selector pin, or NULL having reported.
 *
 * All three failures -- too few inputs, too few outputs, an unwired pin 0 -- are
 * standing conditions: the same thing is wrong on every pass or on none. So they
 * share the sticky VM_BLK_RT_CFG_BAD latch and report once per load rather than
 * a hundred times a second. The error is built *after* the latch test, so the
 * passes that would not report one do not allocate one either.
 *
 * `g_vm_block_fault` is raised on every pass regardless, because it is what
 * cfg.on_error reads -- reporting is once, but the flow stays withdrawn for as
 * long as the block is broken.
 */
static const vm_accessor_t* selector(vm_block_h b, uint8_t min_q) {
  const bool shape = (b->cfg.in_cnt >= 1) && (b->cfg.q_cnt >= min_q);
  const vm_accessor_t* in0 = shape ? vm_block_inputs(b)[0] : NULL;
  if (likely(in0 != NULL)) return in0;

  g_vm_block_fault = true;
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return NULL;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;

  err_h e = shape ? vm_block_err_pin_unlinked(b->cfg.block_idx, 0, false)
                  : SE_ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt,
                               .q_cnt = b->cfg.q_cnt);
  vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  return NULL;
}

/* ==========================================================================
   The bodies

   Both are the same four lines: work out which branch, drive it, assert the
   flow. `taken` starts at BR_NONE and only a clean read off an enabled block
   moves it, so *every* way of not deciding -- disabled, unwired, a resolve that
   failed, a selector past the last case -- converges on one exit that clears
   the lot and withdraws the flow. For a router that is what standing down is:
   leaving an output up would gate a subtree on a decision this block did not
   make.
   ========================================================================== */

void vm_blk_if(vm_block_h b) {
  const vm_accessor_t* in0 = selector(b, 2);
  uint8_t taken = BR_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      float cond = 0.0f;
      err_h e = VM_OBJ_GET_VAL(cond, in0);
      if (unlikely(e)) {
        /* Reported every pass rather than latched, unlike the shape faults
           above: a resolve can fail on this pass's data alone -- a dynamic
           index gone out of range -- and come back on the next. */
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      } else {
        taken = (cond != 0.0f) ? 0u : 1u;
      }
    }
  }

  drive(b, taken);
  vm_block_set_ENO(b, (taken != BR_NONE) && !g_vm_block_fault);
}

void vm_blk_switch(vm_block_h b) {
  const vm_accessor_t* in0 = selector(b, 1);
  uint8_t taken = BR_NONE;

  IF_BLOCK_ENABLED(b) {
    if (likely(in0 != NULL)) {
      int32_t sel = 0;
      err_h e = VM_OBJ_GET_VAL(sel, in0);
      if (unlikely(e)) {
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      } else if (likely(sel >= 0 && sel < (int32_t)b->cfg.q_cnt)) {
        taken = (uint8_t)sel;  // out of range stays BR_NONE: a state, not a fault
      }
    }
  }

  drive(b, taken);
  vm_block_set_ENO(b, (taken != BR_NONE) && !g_vm_block_fault);
}
