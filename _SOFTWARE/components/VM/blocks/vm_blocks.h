#pragma once
#include <stdint.h>
#include "vm_block.h"

/*
The palette -- every block type this firmware can run.

**These are placeholders.** They have the shapes real blocks have and none of
the behaviour: each one decides whether to act exactly as its kind of block
would, then publishes a 1 instead of doing anything. They exist so the
execution layer has something to dispatch before the real palette is written,
and so every path through it -- standing down, holding, claiming a span,
failing -- is actually executed rather than merely compiled.

They are also the templates in core/block/vm_block_template.h made real: one
per shape, so a real block starts by copying the dummy nearest to it.

Type 0 is deliberately unused. A `block_type` nobody set is zero, and it should
not resolve to a runnable block.
*/

#define VM_BLK_NONE 0      // reserved: an unset block_type is not runnable
#define VM_BLK_NOP 1       // no gate at all -- always acts
#define VM_BLK_GATE 2      // enable-driven: acts while its EN sources allow
#define VM_BLK_ON_UPDATE 3 // update-driven: acts when an input carries new data
#define VM_BLK_HOLD 4      // enable-driven actuator: holds everything when inactive
#define VM_BLK_REPEAT 5    // span owner: claims the range after it and walks it
#define VM_BLK_FAULT 6     // fails on purpose, to exercise cfg.on_error
#define VM_BLK_ON_EVENT 7  // walks this cycle's routed callbacks for one it knows

/* Real blocks start here. The id space is one space -- the palette table is
   indexed by it -- so they are numbered from where the placeholders stop
   rather than in a range of their own. Their bodies are declared beside their
   own wire format (vm_block_expr.h), not here: this header is the palette's
   id list plus whatever the placeholders need, and a real block type brings
   its own header along with its own .c. */
#define VM_BLK_EXPR 8      // RPN expression over floats      -- vm_block_expr.h
#define VM_BLK_EXPR_BIT 9  // RPN expression over uint32 bits -- vm_block_expr.h
#define VM_BLK_IF 10       // two-way flow router             -- vm_block_branch.h
#define VM_BLK_SWITCH 11   // n-way flow router               -- vm_block_branch.h
#define VM_BLK_FOR 12      // span owner: repeats the range after it -- vm_block_for.h

/**
 * @brief Private state every placeholder carries.
 *
 * `span` sits first because vm_block_span() reads a vm_span_t off the head of
 * custom_data, so VM_BLK_REPEAT needs it there. The rest carry it too and
 * leave it zero -- one layout for every placeholder is worth more than four
 * bytes per block in code nothing ships behind.
 *
 * The counters are how the placeholders are observable at all: `calls` says
 * the supervisor dispatched to this block, `acts` says the block decided to
 * do something. The gap between them is the whole of activation, and it lives
 * in the block's own private state rather than in a global, because that is
 * what private state is for.
 */
typedef struct vm_dummy_state_t {
  vm_span_t span;      // VM_BLK_REPEAT only; zero elsewhere
  /* VM_BLK_ON_EVENT only: the callback_type_e it recognises. A real block
     would carry a device and pin here too -- what matters is that the filter
     is the *block's* parameter, so nothing addresses an event to a block. */
  uint16_t event_match;
  uint16_t _pad;
  uint32_t calls;      // bumped every dispatch, acted or not
  uint32_t acts;       // bumped when the block decided to act
} vm_dummy_state_t;

/** @brief This block's placeholder state, or NULL if it declared too little
 *  custom_data to hold one.
 *
 *  Size, not identity: a real block's private state can be *larger* than this
 *  and mean something entirely different, so anything reaching into a block it
 *  did not build must check `cfg.block_type <= VM_BLK_ON_EVENT` first. The
 *  placeholders themselves are the intended callers, and they already know. */
static inline vm_dummy_state_t* vm_dummy_state(vm_block_h b) {
  if (b->cfg.custom_len < sizeof(vm_dummy_state_t)) return NULL;
  return (vm_dummy_state_t*)vm_block_custom_data(b);
}

// the bodies, so the palette table can name them
void vm_blk_nop(vm_block_h b);
void vm_blk_gate(vm_block_h b);
void vm_blk_on_update(vm_block_h b);
void vm_blk_hold(vm_block_h b);
void vm_blk_repeat(vm_block_h b);
void vm_blk_fault(vm_block_h b);
void vm_blk_on_event(vm_block_h b);
