#include "vm_blocks.h"
#include "vm_event.h"  // vm_event_take(), for the event shape
#include "vm_exec.h"   // vm_exec_run_range(), for the span owner

#define OWNER OWNER_VM_BLOCK

/*
Placeholder block bodies -- see vm_blocks.h for what they are and are not.

Every one of them follows the same rule, which is the rule for real blocks too:

    A block that does not act says so with vm_block_set_ENO(b, false),
    and does nothing else at all.

Its outputs stand -- nothing changed, so the last answer is still the right one
-- and the supervisor's end-of-pass `upd` sweep withdraws their freshness
without the block lifting a finger. So the only thing that varies between these
is *how they decide*, which is the only thing worth demonstrating before there
is real behaviour to write.
*/

// bumped first thing in every body, before any decision
static vm_dummy_state_t* enter(vm_block_h b) {
  vm_dummy_state_t* s = vm_dummy_state(b);
  if (s) s->calls++;
  return s;
}

/* What "doing something" stands in for. A real block would drive a servo or
   evaluate an expression here; this publishes a 1, which is enough to tell a
   pass it acted from a pass it stood down. */
static void act(vm_block_h b, vm_dummy_state_t* s) {
  if (s) s->acts++;
  if (b->cfg.q_cnt) BLOCK_CALL(VM_OBJ_SET_VAL_AT((uint8_t)1, vm_block_outputs(b)[0], 0), b);
  vm_block_set_ENO(b, true);
}

// ---------------------------------------------------------------------------

/* No gate at all -- the shape ADC, MAP, A > B and every expression block with
   no enable sources have. Always runs, so there is no else-branch to write. */
void vm_blk_nop(vm_block_h b) {
  act(b, enter(b));
}

// Enable-driven: runs on a level. Setters, comparators, anything gated.
void vm_blk_gate(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  IF_BLOCK_ENABLED(b) {
    act(b, s);
  }
  else {
    vm_block_set_ENO(b, false);
  }
}

/* Update-driven, composed with an enable -- "route this event, but only while
   armed". The composition costs nothing: it is two questions asked in a row,
   and nothing outside the block records that it asked both. */
void vm_blk_on_update(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    act(b, s);
    return;
  }
  vm_block_set_ENO(b, false);
}

/* An actuator's `when inactive: Hold`, and the reason that choice needs no
   mechanism: the block is called in both states and simply never sets ENO
   false, so its output and its flow both keep commanding whatever they last
   did. A `Home` variant would instead drive its safe value here. */
void vm_blk_hold(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  IF_BLOCK_ENABLED(b) act(b, s);
}

/*
Span owner -- what FOR will be.

The ordering is the load-bearing part, and it is the one place a block must do
something before deciding anything: **claim first, unconditionally**. The outer
walk jumps over a claimed range; a disabled span owner that returned early
without claiming would leave the walk about to run its range inline, and
running a span zero times is not the same as letting somebody else run it once.

Twice rather than N times because the count is a real block's parameter, and
two is already enough to tell "the owner ran it" from "the walk fell into it".
*/
void vm_blk_repeat(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  if (!s) return;

  vm_span_t range = s->span;  // copied out: the claim writes through the same bytes
  vm_block_claim_span(b, range.start, range.end);

  IF_BLOCK_ENABLED(b) {
    for (int i = 0; i < 2; i++) vm_exec_run_range(range.start, range.end);
    act(b, s);
  }
  else {
    vm_block_set_ENO(b, false);  // claimed, then run zero times
  }
}

/*
Fails on purpose, after publishing -- which is the case cfg.on_error exists for.
A body that failed before writing anything needs nothing done for it; one that
already put a value downstream and *then* went wrong is why VM_BLK_ERR_STOP
exists, and what it does is drop this block's ENO so nothing below acts on what
was published.

Note it does not set ENO false itself. That is the supervisor's, deliberately:
by the time a body knows it failed it may already have published and returned,
so the decision has to sit outside it.
*/
void vm_blk_fault(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  act(b, s);

  const vm_accessor_t* nowhere = NULL;
  BLOCK_CALL(vm_block_get_in(&nowhere, b, 99), b);
}

/*
The event shape: nothing is delivered, the block asks.

It runs like every other block, every pass, and the only thing that makes it
special is where it looks -- it walks this cycle's routed callbacks and picks
out what it recognises. Nothing was addressed to it, nothing woke it, and if
there is no match this cycle it stands down like any other block with nothing
to do.

Nothing is consumed either, which is why "no obligation to target a block"
works: two blocks watching the same GPIO both see the edge, and there is no
ownership to arbitrate. An event lives one scan cycle, so missing one is
possible and normal -- a block whose enable was false when the edge arrived
simply never sees it, the same answer the hardware gives.

This placeholder matches on `callback_type` alone, which is the coarsest filter
there is. A real block would match a device and pin out of its private state as
well, and read the union arm its type says is live.
*/
void vm_blk_on_event(vm_block_h b) {
  vm_dummy_state_t* s = enter(b);
  if (!s) return;

  const cb_event_t* hit = NULL;
  for (uint8_t i = 0; i < vm_event_count(); i++) {
    const cb_event_t* ev = vm_event_at(i);
    if (ev && ev->head.callback_type == s->event_match) {
      hit = ev;
      break;
    }
  }

  if (!hit) {
    vm_block_set_ENO(b, false);
    return;
  }

  /* Published *loudly* -- an arrival genuinely is news, and an update-driven
     block below should see it as one. That is the whole asymmetry: taking an
     event is loud, standing down is quiet. */
  s->acts++;
  if (b->cfg.q_cnt) BLOCK_CALL(VM_OBJ_SET_VAL_AT((uint8_t)1, vm_block_outputs(b)[0], 0), b);
  vm_block_set_ENO(b, true);
}
