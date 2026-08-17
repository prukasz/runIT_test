#include <string.h>
#include "esp_compiler.h"
#include "vm_block.h"

#define OWNER OWNER_VM_BLOCK

/*
The runtime half of a block, split from vm_block_build.c the same way
vm_obj_access.c is split from vm_obj_build.c: construction runs once per load
and may be as careful as it likes, these run once per block per pass.

Everything here is out of line on purpose, and none of it is on the hot path:
the supervisor calls none of it. These are what a *block body* reaches for --
clearing its outputs when it stands down, asking whether anything arrived,
claiming a range -- so each one is paid for only by the blocks that use it.
*/

bool g_vm_block_fault = false;

/* Freshness is a property of the *object* the value lives in, not of the
   element -- `upd` is one bit in the head. So this wants the owner, which is
   what a resolve produces alongside the payload.

   A failure is silent rather than reported: it will be reported again the
   moment the body reads the same pin, and a pin that cannot be read is
   certainly not an arrival. Fail-closed, same as the enable list. */
static inline bool pin_fresh(const vm_accessor_t* acc) {
  if (!acc) return false;  // pin exists, nothing wired to it

  if (likely(acc->flags & VM_ACC_F_CACHED)) {
    return acc->c_owner && acc->c_owner->head.f.upd;
  }

  vm_resolved_t r;
  if (vm_resolve_fast(acc, false, &r)) {
    return r.owner && r.owner->head.f.upd;
  }

  vm_obj_h o = NULL;
  if (vm_get_obj(&o, acc) != NULL || !o) return false;
  return o->head.f.upd != 0;
}

bool vm_block_input_fresh(vm_block_h b, uint8_t pin) {
  if (unlikely(pin >= b->cfg.in_cnt)) return false;
  return pin_fresh(vm_block_inputs(b)[pin]);
}

bool vm_block_triggered(vm_block_h b) {
  const vm_accessor_t** ins = vm_block_inputs(b);
  for (uint8_t i = 0; i < b->cfg.in_cnt; i++) {
    if (pin_fresh(ins[i])) {
      b->cfg.rt |= VM_BLK_RT_TRIGGERED;
      return true;
    }
  }
  return false;
}

void vm_block_claim_span(vm_block_h b, uint16_t start, uint16_t end) {
  /* The range is written into the block's own payload rather than handed to
     the supervisor, so there is exactly one copy of it and the walk reads the
     same bytes the block does. Nothing outside the block needs to know what
     those bytes mean. */
  if (unlikely(b->cfg.custom_len < sizeof(vm_span_t) || end <= start)) {
    if (!(b->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
      /* Sticky: a block claiming a range it has no room to store, or one that
         does not move the walk forward, is a standing condition. Reporting it
         once per program beats once per pass forever, and the walk falls
         through to the next block either way -- the span degrades to running
         inline rather than to a hang. */
      b->cfg.rt |= VM_BLK_RT_SPAN_BAD;
      SE_EMIT_ERR(ERR_VM_EXEC_BAD_SPAN, .block_idx = b->cfg.block_idx, .start = start, .end = end);
    }
    return;
  }

  vm_span_t* sp = (vm_span_t*)vm_block_custom_data(b);
  sp->start = start;
  sp->end = end;
  b->cfg.rt |= VM_BLK_RT_SPAN;
}
