#pragma once
#include "vm_block.h"
#include "vm_event.h"  // vm_event_count() / _at(), for the event shape
#include "vm_exec.h"   // vm_exec_run_range(), for span owners only

/*
How to write a block.

A block type is a function and a line in the palette table (`g_vm_blocks[]` in
blocks/vm_blocks_table.c). The supervisor calls it once per pass, every pass,
and decides nothing else: no activation test, no gating, no skipping. So the
first thing every body does is decide for itself whether to act.

  ONE RULE UNDERNEATH ALL OF IT

    A block that does not act says so with vm_block_set_ENO(b, false),
    and does nothing else at all.

  Its outputs stand. Nothing changed, so last pass's answer is still the right
  one -- clearing it would publish a 0 where a correct value was standing. Data
  persists; what is withdrawn is the *flow*, and ENO is what carries that.

  Nothing else is needed because the supervisor already withdraws freshness for
  everybody: it sweeps `upd` at the end of every pass, so an output nobody
  rewrote reads as stale on the next one for free. Between the ENO level and
  that sweep, both kinds of downstream block stand down on their own -- one
  gated on the level, one waiting on an arrival.

  The write is asymmetric and vm_block_set_ENO() handles it: **true is loud**
  (an assertion of flow is news, so it sets `upd`), **false is quiet** (a loud
  false would read as an *arrival* to an update-driven block below, the exact
  opposite of standing down). No block body has to know that.

Two questions a body can open with, both plain `if`, so both pair with `else`:

  IF_BLOCK_TRIGGERED(b)   did anything new arrive on one of my inputs?
  IF_BLOCK_ENABLED(b)     do my enable sources say so?

They nest in either order, which is all "the two groups compose" ever meant.
There is no IF_NOT_ form on purpose: two macros around two braces would ask the
same question twice, and for freshness that is two full scans of the inputs.
*/

/* ==========================================================================
   Template 1 -- update-driven

   Nothing to do unless something new arrived. Event routers, message decoders,
   expression blocks whose inputs all come off wires.
   ========================================================================== */

// EXPRESSION BLOCK -- all inputs old means the result is unchanged, so the
// output stays exactly where it is and only the flow drops.
static inline void template_expression(vm_block_h b) {
  IF_BLOCK_TRIGGERED(b) {
    // ... evaluate the expression in custom_data, publish to output 0 ...
    vm_block_set_ENO(b, true);
  }
  else {
    vm_block_set_ENO(b, false);
  }
}

/* Both questions at once -- "route this event, but only while armed". Nothing
   outside the block records that it is both; it simply asks twice. */
static inline void template_armed_router(vm_block_h b) {
  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    // ... read the event, drive whichever branch output matches ...
    vm_block_set_ENO(b, true);
    return;
  }
  vm_block_set_ENO(b, false);
}

/* ==========================================================================
   Template 2 -- enable-driven

   Runs on a level rather than on an arrival. Actuators, setters, anything
   whose job is to keep commanding while it is allowed to.
   ========================================================================== */

// SERVO -- and the reason `when inactive:` needs no mechanism at all: the
// block is called in both states, and the disabled branch is whatever the
// user chose, read out of the block's own custom_data.
static inline void template_servo(vm_block_h b) {
  IF_BLOCK_ENABLED(b) {
    // ... read input 0, drive the angle ...
    vm_block_set_ENO(b, true);
  }
  else {
    // "when inactive:" -- Home drives the safe angle and keeps commanding;
    // Hold returns without touching a thing, ENO included, so the output and
    // the flow both stay exactly where they were.
    vm_block_set_ENO(b, false);
  }
}

/* ==========================================================================
   Template 3 -- span owner

   A block that repeats or skips part of the order. FOR is the only one so far.
   ========================================================================== */

/* The ordering here is the load-bearing part, and it is the one place a block
   must do something *before* deciding anything: claim the span first, always.
   The outer walk jumps over a claimed range; a disabled FOR that returned
   early without claiming would leave the walk about to run its span inline,
   and running a span zero times is not the same as letting somebody else run
   it once. */
static inline void template_for(vm_block_h b) {
  const vm_span_t* sp = vm_block_span(b);
  if (!sp) return;
  vm_span_t range = *sp;                              // copy: claim writes through sp
  vm_block_claim_span(b, range.start, range.end);     // first, unconditionally

  IF_BLOCK_ENABLED(b) {
    uint32_t n = 0;  // ... bounded count, out of custom_data ...
    for (uint32_t i = 0; i < n; i++) vm_exec_run_range(range.start, range.end);
    vm_block_set_ENO(b, true);
  }
  else {
    vm_block_set_ENO(b, false);  // claimed, then run zero times
  }
}

/* ==========================================================================
   Template 4 -- events

   Nothing is delivered and nothing is addressed to a block. The block walks
   this scan cycle's routed callbacks and picks out what it recognises.
   ========================================================================== */

/* Nothing is consumed, so two blocks watching the same GPIO both see the edge
   -- there is no ownership to arbitrate. An event lives one scan cycle, so
   missing one is possible and normal: a block whose enable was false when the
   edge arrived simply never sees it, the same answer the hardware gives. */
static inline void template_on_event(vm_block_h b) {
  for (uint8_t i = 0; i < vm_event_count(); i++) {
    const cb_event_t* ev = vm_event_at(i);
    if (!ev || ev->head.callback_type != CALLBACK_IO) continue;
    // ... match the device and pin out of custom_data, publish the payload ...
    vm_block_set_ENO(b, true);
    return;
  }
  vm_block_set_ENO(b, false);
}

/* ==========================================================================
   Reporting

   An execute() body is void: there is nobody above it to return an err_h to.
   So failures are *reported* rather than propagated, and cfg.on_error decides
   what that does to the flow below -- which is the supervisor's job, not the
   block's, because by the time a body knows it failed it may already have
   published and returned.

   Wrap every err_h-returning call in BLOCK_CALL, which attaches this block's
   identity and leaves the mark the supervisor reads:

     BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), b);

   Under VM_BLK_ERR_STOP the supervisor then drops this block's ENO, so nothing
   below acts on whatever was published. Do not do that yourself on an error
   path -- a body cannot tell whether it is about to fail again, and the
   decision belongs where it can see the outcome.
   ========================================================================== */
