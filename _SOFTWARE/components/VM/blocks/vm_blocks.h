#pragma once

/*
The palette -- every block type this firmware can run.

This header is the id list and nothing else. A block type is a function plus a
wire format, and both of those live with the block: `vm_block_expr.h` carries
the RPN header, opcodes and evaluator, `vm_block_branch.h` the routers,
`vm_block_for.h` the loop. What has to be central is only the numbering,
because the id *is* the index into `g_vm_blocks[]` (blocks/vm_blocks_table.c)
and a firmware has exactly one palette.

Adding a block type is its own header beside these, one `#define` here, and one
line in the table.

Type 0 is deliberately unused. A `block_type` nobody set is zero, and it should
not resolve to a runnable block.
*/

#define VM_BLK_NONE 0      // reserved: an unset block_type is not runnable
#define VM_BLK_EXPR 1      // RPN expression over floats      -- vm_block_expr.h
#define VM_BLK_EXPR_BIT 2  // RPN expression over uint32 bits -- vm_block_expr.h
#define VM_BLK_IF 3        // two-way flow router             -- vm_block_branch.h
#define VM_BLK_SWITCH 4    // n-way flow router               -- vm_block_branch.h
#define VM_BLK_FOR 5       // span owner: repeats the range after it -- vm_block_for.h
#define VM_BLK_SET 6       // copies a payload, source -> target      -- vm_block_set.h
#define VM_BLK_CLONE 7     // copies, building the destination first  -- vm_block_clone.h
#define VM_BLK_EDGE 8      // edge detector (rising, falling, both)   -- vm_block_edge.h
#define VM_BLK_TIMER 9     // timer (TON, TOF, TP + inverted)         -- vm_block_timer.h
