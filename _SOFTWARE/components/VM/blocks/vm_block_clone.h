#pragma once
#include "esp_compiler.h"
#include "vm_block.h"

#define VM_CLONE_CUSTOM_LEN 0u

#define VM_CLONE_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_CLONE_IN_CELL 1u // target -- a pointer cell this block re-points

/*
VM_BLK_CLONE -- like VM_BLK_SET, except it builds the destination instead of
requiring one. Update-driven and stateless (custom_len = 0).

Where a Set copies into an object the program already declared, a Clone owns
what it writes into: IN1 names a *pointer cell*, and the block keeps a tree of
its own hanging off it. That is what a Set cannot do, because a Set has to be
told the destination's shape at build time, and a tree that arrived off the
wire does not have one until it arrives.

Allocation happens on a schema change, including tag identity. The first pass builds
the tree; every pass after that finds a destination that already matches and
copies values into it, which is the same allocation-free walk a Set does. A
source whose schema varies pays one build each time it changes. The replacement
is filled before publication, and the previous tree is then released by its slot --
see slot_store() in vm_obj_access.c, where reference counts move.

The clone is heap-backed (vm_obj_dyn), because the arena cannot free and this
destination has to be replaceable. That is a real departure from "no dynamic
allocation after load", and it is confined to this block: nothing else in a
running program allocates, and a program with no Clone in it still cannot.
*/

/* Both pins or neither, reported once and latched -- the same standing
   configuration fault a Set raises, for the same reason. */
static inline bool vm_clone_pins(vm_block_h b, const vm_accessor_t** src, const vm_accessor_t** cell) {
  const bool shape = (b->cfg.in_cnt >= 2);
  const vm_accessor_t** in = vm_block_inputs(b);

  if (likely(shape && in[VM_CLONE_IN_SRC] != NULL && in[VM_CLONE_IN_CELL] != NULL)) {
    *src = in[VM_CLONE_IN_SRC];
    *cell = in[VM_CLONE_IN_CELL];
    return true;
  }

  g_vm_block_fault = true;
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return false;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;

  err_h e = shape ? vm_block_err_pin_unlinked(b->cfg.block_idx, in[VM_CLONE_IN_SRC] ? VM_CLONE_IN_CELL : VM_CLONE_IN_SRC, false)
                  : VM_BLK_ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt,
                                   .q_cnt = b->cfg.q_cnt);
  vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  return false;
}

static inline void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = NULL;
  const vm_accessor_t* cell = NULL;

  if (likely(vm_clone_pins(b, &src, &cell))) {
    /* IN0 alone, exactly as in a Set: the cell this block writes is an input
       pin too, and re-pointing it sets `upd` on its owner, so a trigger over
       both pins would fire the block on its own last allocation. */
    if (vm_block_input_fresh(b, VM_CLONE_IN_SRC)) {
      b->cfg.rt |= VM_BLK_RT_TRIGGERED;

      IF_BLOCK_ENABLED(b) {
        BLOCK_CALL(vm_obj_clone_into_usr(src, cell), b);
        if (likely(!g_vm_block_fault)) {
          vm_block_set_ENO(b, true);
          return;
        }
      }
    }
  }

  vm_block_set_ENO(b, false);
}
