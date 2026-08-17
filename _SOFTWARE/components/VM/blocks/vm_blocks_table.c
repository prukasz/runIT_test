#include "vm_blocks.h"
#include "vm_block_branch.h"
#include "vm_block_expr.h"
#include "vm_block_for.h"
#include "vm_exec.h"

/*
The palette, entire.

`const`, so it lives in flash and costs no RAM; a designated initialiser per
entry, so the index *is* the `block_type` on the wire; and one definition in
one file, because a firmware has exactly one palette. Adding a block type is
its own `.c` plus one line here.

It lives beside the blocks rather than in core/exec/ on purpose. The supervisor
must not know what is in the palette -- it dispatches through the declaration
in vm_exec.h and nothing more -- so the dependency runs blocks -> exec, never
back. That is what keeps `core/` free of every driver a real block will
eventually pull in.

C lets a later designated initialiser silently override an earlier one, so two
types claiming one id would compile clean and the last would win. The component
builds with -Woverride-init to make that a warning instead.
*/
const vm_block_fn g_vm_blocks[] = {
    /* [VM_BLK_NONE] stays NULL: an unset block_type must not be runnable. */
    [VM_BLK_NOP] = vm_blk_nop,
    [VM_BLK_GATE] = vm_blk_gate,
    [VM_BLK_ON_UPDATE] = vm_blk_on_update,
    [VM_BLK_HOLD] = vm_blk_hold,
    [VM_BLK_REPEAT] = vm_blk_repeat,
    [VM_BLK_FAULT] = vm_blk_fault,
    [VM_BLK_ON_EVENT] = vm_blk_on_event,
    [VM_BLK_EXPR] = vm_blk_expr,
    [VM_BLK_EXPR_BIT] = vm_blk_expr_bit,
    [VM_BLK_IF] = vm_blk_if,
    [VM_BLK_SWITCH] = vm_blk_switch,
    [VM_BLK_FOR] = vm_blk_for,
};

const uint16_t g_vm_blocks_cnt = (uint16_t)(sizeof(g_vm_blocks) / sizeof(g_vm_blocks[0]));
