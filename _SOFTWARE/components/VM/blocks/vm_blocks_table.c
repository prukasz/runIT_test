#include "vm_blocks.h"
#include "vm_block_branch.h"
#include "vm_block_expr.h"
#include "vm_block_for.h"
#include "vm_block_clone.h"
#include "vm_block_set.h"
#include "vm_block_edge.h"
#include "vm_block_timer.h"

/*
The palette, entire.

`const`, so it lives in flash and costs no RAM; a designated initialiser per
entry, so the index *is* the `block_type` on the wire; and one definition in
one file, because a firmware has exactly one palette. Adding a block type is
its own header beside this one, plus one line here.

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
    [VM_BLK_EXPR] = vm_blk_expr,
    [VM_BLK_EXPR_BIT] = vm_blk_expr_bit,
    [VM_BLK_IF] = vm_blk_if,
    [VM_BLK_SWITCH] = vm_blk_switch,
    [VM_BLK_FOR] = vm_blk_for,
    [VM_BLK_SET] = vm_blk_set,
    [VM_BLK_CLONE] = vm_blk_clone,
    [VM_BLK_EDGE] = vm_blk_edge,
    [VM_BLK_TIMER] = vm_blk_timer,
};

const vm_block_verify_fn g_vm_blocks_verify[] = {
    [VM_BLK_EXPR] = vm_verify_expr,
    [VM_BLK_EXPR_BIT] = vm_verify_expr,
    [VM_BLK_IF] = vm_verify_if,
    [VM_BLK_SWITCH] = vm_verify_switch,
    [VM_BLK_FOR] = vm_verify_for,
    [VM_BLK_SET] = vm_verify_set,
    [VM_BLK_CLONE] = vm_verify_clone,
    [VM_BLK_EDGE] = vm_verify_edge,
    [VM_BLK_TIMER] = vm_verify_timer,
};

const uint16_t g_vm_blocks_cnt = (uint16_t)(sizeof(g_vm_blocks) / sizeof(g_vm_blocks[0]));
