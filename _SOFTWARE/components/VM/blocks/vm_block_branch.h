#pragma once
#include "vm_block.h"

/*
Flow routers -- VM_BLK_IF (two-way) and VM_BLK_SWITCH (up to sixteen).

Both read one input and drive a one-hot set of boolean outputs meant to be read
as *enable sources* by the blocks below them. They carry no custom_data at all:
`in_cnt` names the selector pin, `q_cnt` names the branch count, and there is
nothing else to say. That also makes them stateless -- the one-hot is recomputed
from the input on every pass, and nothing is carried between them.

  WHY THESE ARE ENABLE-DRIVEN, AND WHY THAT IS NOT A STYLE CHOICE

  The one rule in core/block/vm_block_template.h is that a block which does not
  act withdraws the *flow* and leaves its outputs standing -- because an output
  holds data, and last pass's data is still the right answer.

  A router has no data. Its outputs *are* the flow, so they follow the flow's
  rule rather than the data's: rewritten on every pass the block is enabled,
  cleared on every pass it is not. One that stood down leaving branch 3 high
  would hold the whole subtree below it enabled through a closed gate -- and
  because branch outputs feed enable lists, that leak propagates down the DAG
  instead of stopping at one block.

  The same reasoning is why these blocks retract their own outputs on a failure,
  which no other block should do. `cfg.on_error` is the supervisor's to apply
  and all it can reach is ENO (`run_block()` in vm_exec.c) -- the right and
  sufficient net for every block whose outputs are data, and no net at all for
  one whose outputs gate.

  Clearing is *quiet*, exactly as vm_block_set_ENO(b, false) is: a loud false
  reads as an arrival to an update-driven block below, which is the opposite of
  standing down.

  WHAT EACH ONE SELECTS

  IF      reads the pin as a float and takes output 0 when it is non-zero,
          output 1 otherwise. Truthiness is `!= 0.0f`, the same test the
          expression palette's AND/OR/XOR/NOT use, so a condition computed in
          an EXPR block and a condition tested here always agree. (A NaN is
          therefore truthy. EXPR refuses to publish one -- VM_EXPR_MATH_NOT_FINITE
          -- so it can only arrive from a directly written object.)

  SWITCH  reads the pin as an int32 and takes the output of that index. A float
          source therefore *rounds*, and saturates rather than overflowing --
          vm_read_as_i32() does that for every float-to-integer pin read in the
          VM, and a router that truncated instead would be the single pin in the
          program that converts its own way. So 2.6 selects branch 3; a program
          wanting the other thing puts an EXPR `TRUNC` in front. Case values are
          dense -- branch index *is* the selector value -- because remapping a
          sparse set onto 0..n-1 is one EXPR block and would otherwise cost every
          switch in every program a 64-byte table it does not use.

          A selector outside 0..q_cnt-1 takes no branch: every output clears and
          ENO goes false. That is an ordinary runtime state rather than an error,
          so it is not reported -- at scan rate it would bury the log. A program
          wanting a default branch drives one from an EXPR range test.
*/

/** @brief Bytes of custom_data either router needs. Neither has private state:
 *  everything they read lives in `cfg` and the pin arrays. */
#define VM_BRANCH_CUSTOM_LEN 0u

// the bodies, so the palette table can name them
void vm_blk_if(vm_block_h b);
void vm_blk_switch(vm_block_h b);
