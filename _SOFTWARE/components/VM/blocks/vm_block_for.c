#include "vm_block_for.h"
#include <math.h>
#include "esp_compiler.h"
#include "vm_exec.h"  // vm_exec_run_range() -- the one block that needs it

#define OWNER OWNER_VM_BLOCK

/*
The span owner. See vm_block_for.h for why it claims before it decides anything
and why the turn budget is a budget rather than a count; what is left here is the
order of operations, which is the only part that is easy to get wrong.
*/

static vm_for_code_t* code_of(vm_block_h b) {
  if (unlikely(b->cfg.custom_len < sizeof(vm_for_code_t))) return NULL;
  vm_for_code_t* c = (vm_for_code_t*)vm_block_custom_data(b);
  if (unlikely(c->op >= VM_FOR_OP_CNT || c->cmp >= VM_FOR_CMP_CNT)) return NULL;
  return c;
}

/** @brief Latch and report a malformed FOR once per load. The error is built
 *  after the latch test, so the passes that would not report one do not
 *  allocate one either. */
static void cfg_bad(vm_block_h b) {
  g_vm_block_fault = true;
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;
  SE_EMIT_ERR(ERR_VM_BLK_BAD_SHAPE, .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt, .q_cnt = b->cfg.q_cnt);
}

/** @brief A loop that could not end on its own. Sticky for the same reason
 *  every standing condition is: this runs at scan rate. */
static void bad_loop(vm_block_h b, vm_for_code_t* c, uint32_t turns, uint8_t reason) {
  if (c->rt & VM_FOR_RT_BAD) return;
  c->rt |= VM_FOR_RT_BAD;
  SE_EMIT_ERR(ERR_VM_FOR_BAD_LOOP, .block_idx = b->cfg.block_idx, .turns = turns, .cap = c->max_turns,
              .reason = reason);
}

/**
 * @brief One loop parameter: the pin if there is one, the constant otherwise.
 *
 * An undeclared or unwired pin reading its constant is the standing rule for
 * every block, not a special case here. A pin that is wired and cannot be *read*
 * is different: the program said to take this number from somewhere, and
 * substituting a constant would run a loop nobody asked for. That returns false
 * and the loop runs zero times.
 */
static bool param(float* out, vm_block_h b, uint8_t pin, float k) {
  *out = k;
  if (pin >= b->cfg.in_cnt) return true;

  const vm_accessor_t* a = vm_block_inputs(b)[pin];
  if (!a) return true;

  err_h e = VM_OBJ_GET_VAL(*out, a);
  if (unlikely(e)) {
    vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
    return false;
  }
  return true;
}

static float advance(uint8_t op, float i, float step) {
  switch (op) {
    case VM_FOR_OP_ADD: return i + step;
    case VM_FOR_OP_SUB: return i - step;
    case VM_FOR_OP_MUL: return i * step;
    default: return i / step;  // DIV; a zero step goes non-finite and is caught
  }
}

static bool keep_going(uint8_t cmp, float i, float end) {
  switch (cmp) {
    case VM_FOR_CMP_LT: return i < end;
    case VM_FOR_CMP_LE: return i <= end;
    case VM_FOR_CMP_GT: return i > end;
    default: return i >= end;  // GE
  }
}

void vm_blk_for(vm_block_h b) {
  /* Claim first, whatever else turns out to be wrong -- including the span
     itself. vm_block_claim_span() rejects a range it cannot store or that does
     not move the walk forward, and reports that once; what it must not do is go
     unattempted, because the walk runs an unclaimed range inline. */
  const vm_span_t* sp = vm_block_span(b);
  const vm_span_t range = sp ? *sp : (vm_span_t){0, 0};  // copied: the claim writes through sp
  vm_block_claim_span(b, range.start, range.end);

  /* Did the claim take? If it did not, the walk is about to run these blocks
     itself, and running them here as well would execute the body twice. */
  const bool owned = (b->cfg.rt & VM_BLK_RT_SPAN) != 0;

  vm_for_code_t* c = code_of(b);
  if (unlikely(!c)) {
    cfg_bad(b);
    vm_block_set_ENO(b, false);
    return;
  }

  float i = 0.0f, end = 0.0f, step = 0.0f;
  bool go = owned;
  IF_BLOCK_ENABLED(b) {
    go = go && param(&i, b, VM_FOR_IN_START, c->k_start);
    go = go && param(&end, b, VM_FOR_IN_END, c->k_end);
    go = go && param(&step, b, VM_FOR_IN_STEP, c->k_step);
    // a parameter that is already non-finite has no loop to describe
    go = go && isfinite(i) && isfinite(end) && isfinite(step);
  } else {
    go = false;
  }

  if (!go || !keep_going(c->cmp, i, end)) {
    // disabled, unclaimed, unreadable, or a condition false before the first
    // turn: claimed, and then run no times
    vm_block_set_ENO(b, false);
    return;
  }

  /* ENO goes true *before* the body rather than after it. A block inside the
     span gated on this FOR's ENO has to see it on turn 0; asserting afterwards
     would gate the first turn on last pass's answer, and on the first pass on
     nothing at all. The flow is genuinely asserted for as long as the body is
     running, so this is also just what ENO means. cfg.on_error still withdraws
     it afterwards if anything failed. */
  vm_block_set_ENO(b, true);

  vm_obj_h idx = (b->cfg.q_cnt >= 1) ? vm_block_outputs(b)[0] : NULL;
  const uint32_t budget = c->max_turns;
  uint32_t turns = 0;

  while (keep_going(c->cmp, i, end)) {
    if (unlikely(turns >= budget)) {
      // the condition is still true and the budget is gone: this loop does not
      // end on its own, which is exactly what the budget exists to survive
      bad_loop(b, c, turns, VM_FOR_BAD_CAPPED);
      return;
    }

    if (idx) {
      /* Loud, so an update-driven block in the body re-triggers every turn. A
         failure here stops the loop: every turn would fail identically, and a
         body reading a stale iterator is worse than a body that does not run. */
      err_h e = VM_OBJ_SET_VAL_AT(i, idx, 0);
      if (unlikely(e)) {
        vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
        return;
      }
    }

    vm_exec_run_range(range.start, range.end);
    turns++;

    i = advance(c->op, i, step);
    if (unlikely(!isfinite(i))) {
      bad_loop(b, c, turns, VM_FOR_BAD_NOT_FINITE);
      return;
    }
  }

  // ended on its own condition, which re-arms the report
  c->rt &= (uint8_t)~VM_FOR_RT_BAD;
}
