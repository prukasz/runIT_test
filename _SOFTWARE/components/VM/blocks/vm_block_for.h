#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vm_block.h"

/*
VM_BLK_FOR -- the span owner, and the only block that runs other blocks.

It is a C `for` loop over a float iterator:

    for (i = start; i <cmp> end; i = i <op> step)

with `start`, `end` and `step` each coming from an input pin *or*, when that pin
is unwired, from a constant in the block's own custom_data. A loop with nothing
wired is a fully static one; a loop with `end` wired walks however many elements
the array actually holds. `cmp` and `op` are always the block's own, because they
are the shape of the loop rather than data flowing through it.

Layout in custom_data:
  [0..3]   vm_span_t span       the range this block owns; MUST be first
  [4..7]   f32       k_start    used when input 0 is unwired
  [8..11]  f32       k_end      used when input 1 is unwired
  [12..15] f32       k_step     used when input 2 is unwired
  [16..17] u16       max_turns  the hard turn budget -- see below
  [18]     u8        op         VM_FOR_OP_*  -- how the iterator advances
  [19]     u8        cmp        VM_FOR_CMP_* -- while what is true
  [20]     u8        rt         runtime latch (VM_FOR_RT_*); 0 on the wire
  [21..23] u8[3]     pad

`span` is first because that is not this block's choice: vm_block_span() reads a
vm_span_t off the head of any block's custom_data and vm_block_claim_span()
writes one back through the same bytes, so the walk and the block read one copy
of the range rather than two that can disagree.

  CLAIM FIRST, UNCONDITIONALLY

  Before the enable test, before the pins, before anything. The outer walk jumps
  over a claimed range and runs an unclaimed one inline, so a FOR that returned
  early without claiming -- disabled, malformed, zero turns -- would leave its
  body about to execute once, uncontrolled, in the middle of the walk. Running a
  span zero times and letting somebody else run it once are opposite outcomes,
  and only the claim distinguishes them. That is also why a malformed FOR still
  claims whatever span it can read: for a body that drives actuators, once is
  strictly worse than never.

  max_turns IS A BUDGET, NOT THE COUNT

  The loop runs its real condition, so the number of turns is whatever `start`,
  `end`, `step` and `op` work out to -- and those come from pins, which means a
  program can phrase a loop that never ends. `step` of zero. `MUL` by one. A step
  pointing away from `end`. This runs inside a section on core 1, where the block
  watchdog cannot help: it names whichever *body* block is currently running, and
  those keep changing, so a spinning FOR looks like healthy progress right up
  until the task watchdog resets the board.

  So `max_turns` is checked every turn and ends the loop when it is reached. It
  is what keeps a section's worst case computable (`max_turns * span`) whatever
  the pins say, and it turns every non-terminating loop into a bounded one that
  reports itself (ERR_VM_FOR_BAD_LOOP, VM_FOR_BAD_CAPPED) instead of hanging.
  Hitting it always means the loop did not finish on its own, so it is always
  worth reporting -- once per episode, re-armed by a loop that ends properly.

  THE ITERATOR IS A FLOAT, AND ACCUMULATES

  `i = i <op> step` is applied to the running value, exactly as C does it, rather
  than recomputed as `start + n*step`. That is the semantics the name promises,
  and it is the only definition that means anything for MUL and DIV. The cost is
  C's cost: a fractional step drifts, so `0.0` stepping by `0.1` toward `1.0` may
  turn nine times or eleven. Integer starts and steps are exact to 2^24, far past
  any loop that could finish inside a scan cycle.

  The iterator is published to output 0 before each turn, *loudly*, so an
  update-driven block in the body re-triggers every turn rather than only on the
  first. That object is what a by-ref accessor in the body roots its index at,
  which is the whole mechanism for walking an array -- wire a U32 object to it
  and the pin conversion rounds the float for you. A FOR with no outputs is a
  plain repeat, which is a real thing to want.

  Accumulating across turns needs no mechanism either: `upd` is swept at the end
  of the *pass*, not per turn, so a block inside the span reading an object it
  also writes sees its own last value and re-triggers. A sum is one EXPR wired
  back to itself, plus something ahead of the loop to zero the accumulator.
*/

/** @brief How the iterator advances. DIV by a zero step, and MUL from a zero
 *  start, both leave the iterator somewhere the loop cannot leave -- which the
 *  turn budget ends and reports rather than the block trying to outguess. */
typedef enum vm_for_op_e {
  VM_FOR_OP_ADD = 0,
  VM_FOR_OP_SUB,
  VM_FOR_OP_MUL,
  VM_FOR_OP_DIV,
  VM_FOR_OP_CNT
} vm_for_op_e;

/** @brief While what is true. No equality test on purpose: `i != end` on a
 *  float is the one loop condition that misses its own exit. */
typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,  // ascending, the usual one
  VM_FOR_CMP_LE,
  VM_FOR_CMP_GT,      // descending
  VM_FOR_CMP_GE,
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

/** @brief The head of a FOR block's custom_data -- see the layout above. */
typedef struct vm_for_code_t {
  vm_span_t span;     // [0..3]   the owned range; first, because vm_block_span() says so
  float k_start;      // [4..7]   fallbacks for the three pins, used when unwired
  float k_end;        // [8..11]
  float k_step;       // [12..15]
  uint16_t max_turns; // [16..17] the hard budget, not the count
  uint8_t op;         // [18]     vm_for_op_e
  uint8_t cmp;        // [19]     vm_for_cmp_e
  uint8_t rt;         // [20]     runtime latch (VM_FOR_RT_*); 0 on the wire
  uint8_t _pad[3];    // [21..23]
} vm_for_code_t;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off the head of custom_data");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "the literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "the header is a wire format");

/** @brief Input pins, each optional -- an unwired one reads its constant. */
#define VM_FOR_IN_START 0u
#define VM_FOR_IN_END 1u
#define VM_FOR_IN_STEP 2u

/** @brief Sticky within one bad-loop episode: reported once, and re-armed by a
 *  loop that ends on its own condition. */
#define VM_FOR_RT_BAD 0x01u

/* ERR_VM_FOR_BAD_LOOP reasons */
#define VM_FOR_BAD_CAPPED 0u      // ran the whole budget without the condition going false
#define VM_FOR_BAD_NOT_FINITE 1u  // the iterator left the reals -- overflow, or DIV by a zero step

// the body, so the palette table can name it
void vm_blk_for(vm_block_h b);
