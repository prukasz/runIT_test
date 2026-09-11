#pragma once
#include "esp_compiler.h"
#include "vm_block_support.h"

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

static inline bool vm_verify_clone(vm_block_h b) {
  return vm_block_require(b, 2, 0, 0x3u);
}

static inline void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = NULL;
  const vm_accessor_t* cell = NULL;

  if (likely(vm_block_require(b, 2, 0, 0x3u))) {
    src = vm_block_get_inputs(b)[VM_CLONE_IN_SRC];
    cell = vm_block_get_inputs(b)[VM_CLONE_IN_CELL];
    /* IN0 alone, exactly as in a Set: the cell this block writes is an input
       pin too, and re-pointing it sets `upd` on its owner, so a trigger over
       both pins would fire the block on its own last allocation. */
    if (vm_block_triggered_by(b, VM_CLONE_IN_SRC)) {

      IF_BLOCK_ENABLED(b) {
        BLOCK_CALL(vm_obj_clone_into_usr(src, cell), b);
        if (likely(!g_vm_block_fault)) {
          vm_block_set_eno(b, true);
          return;
        }
      }
    }
  }

  vm_block_set_eno(b, false);
}
