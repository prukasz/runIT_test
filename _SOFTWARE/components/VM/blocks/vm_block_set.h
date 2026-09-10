#pragma once
#include "esp_compiler.h"
#include "vm_block_support.h"

#define VM_SET_CUSTOM_LEN 0u

#define VM_SET_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_SET_IN_DST 1u  // target -- named, not read

/*
VM_BLK_SET -- copies the source payload into the target. Update-driven and
stateless (custom_len = 0).

This is the explicit Copy the execution model asks for on any path from an
event output to a consumer that sits behind a time gate: an event object holds
the *latest* payload rather than the one that activated a given block, so a
block resuming in a later pass has to have taken its own snapshot while the
triggering pass was still current.

Both pins are inputs, which is unlike every other block, and it is why a Set
has no data output -- what it produces is already named by the wire the user
drew into IN1. ENO is the only output and reports one thing: the copy happened
this pass.

Whole payloads, not scalars, and the whole tree under them. A Set moves an
array as readily as a single value, and a table of rows as readily as either:
vm_obj_copy_content() walks a VM_OBJ_PTR array rather than copying its bytes,
so the two trees keep their own children and an edit made through the copy
never reaches the original. What it will not do is convert -- the two sides
must agree on type and element count at every leaf, so a float source into a
u32 target is a wiring error, reported, not a silent narrowing.

Nothing is allocated. The destination supplies the shape, so its rows have to
exist already and match the source's; a table shaped differently from the one
being copied into it is a wiring error too.
*/

static inline void vm_blk_set(vm_block_h b) {
  const vm_accessor_t* src = NULL;
  const vm_accessor_t* dst = NULL;

  if (likely(vm_block_require(b, 2, 0, 0x3u))) {
    src = vm_block_get_inputs(b)[VM_SET_IN_SRC];
    dst = vm_block_get_inputs(b)[VM_SET_IN_DST];
    /* Freshness is asked of IN0 alone rather than through vm_block_triggered().
       The target is an input pin like any other, so a general trigger would
       also fire on the object this block just wrote -- and on anything else
       writing it -- turning one arrival into a copy that repeats for as long
       as somebody keeps the target fresh. */
    if (vm_block_triggered_by(b, VM_SET_IN_SRC)) {

      IF_BLOCK_ENABLED(b) {
        BLOCK_CALL(vm_obj_copy_content_usr(src, dst), b);
        if (likely(!g_vm_block_fault)) {
          vm_block_set_eno(b, true);
          return;
        }
      }
    }
  }

  vm_block_set_eno(b, false);
}
