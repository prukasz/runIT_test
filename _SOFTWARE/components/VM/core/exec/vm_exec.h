#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_timer.h"
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block.h"
#include "vm_section.h"

/*
The supervisor -- what actually runs a loaded program.

Free-running; power management is explicitly not a goal. A pass alternates
between a freeze point and a section:

    freeze -> run section 0 (atomically) -> freeze -> run section 1 -> ...
    ... -> end of pass: sample subscriptions, then clear `upd`

A section runs to completion with nothing intervening. Everything external
happens at a freeze point: pending events become visible to their blocks,
remote overrides are applied, live data patches are committed, the buffer swap
is taken, and freeze/resume take effect. That is what section granularity buys
-- freeze latency bounded by the longest *section* rather than by a whole pass
over 300 blocks.

There is no scheduling policy to state and no starvation to counter. Every
section runs every pass; the work saved is inside the blocks rather than in
choosing between them.

**Runs on core 1.** The VM gets the second core so a tight loop cannot starve
BLE, which lives on core 0. That is not quite enough on its own: ESP-IDF
watches the idle task of *both* cores, and taskYIELD() will not feed it because
idle runs at priority 0. So the loop yields one tick per pass -- with
CONFIG_FREERTOS_HZ = 100 that is exactly the 10 ms floor already promised to
the user, and it leaves core 1 usable by device drivers.

Everything runs on this one task. A callback or ISR only enqueues (see
vm_event.h); it never touches a block, an object or the arena. That keeps the
whole store/accessor layer lock-free, and it is why "override only after a
cycle, never mid" falls out automatically rather than needing a rule.
*/

/* ==========================================================================
   Dispatch

   A block type is a function, and the mapping from `block_type` to that
   function is a flat `const` table in blocks/vm_blocks_table.c -- in flash,
   costing no RAM and no start-up code. A designated initialiser *is* the
   registration:

     const vm_block_fn g_vm_blocks[] = {
         [VM_BLK_EXPR] = vm_blk_expr,
         [VM_BLK_IF]   = vm_blk_if,
     };

   There was a registration API here, on the argument that a central table
   makes adding a block type a two-file edit. It is, and that turned out to be
   the better trade: a table you can read at a glance beats a mapping you have
   to grep for, and everything registration needed -- a mutable array, a
   duplicate check, a slot-range check, an init order -- disappears with it.
   Both of its errors became compile-time. (One gap: C lets a later designated
   initialiser silently override an earlier one, so the component builds with
   -Woverride-init to make a doubly-claimed type a warning rather than a
   last-one-wins surprise.)

   A firmware has exactly one palette, so there is no indirection either: the
   array itself is what dispatch indexes, const and in flash, and nothing can
   point it somewhere else.
   ========================================================================== */

/** @brief The palette: `block_type` -> function. Defined once, beside the
 *  blocks, in blocks/vm_blocks_table.c -- so the supervisor never learns what
 *  is in it and `core/` stays free of the drivers a real block pulls in. */
extern const vm_block_fn g_vm_blocks[];
extern const uint16_t g_vm_blocks_cnt;

/** @brief type -> function, NULL for a type the table does not fill. */
static inline vm_block_fn vm_block_fn_for(uint8_t block_type) {
  return (block_type < g_vm_blocks_cnt) ? g_vm_blocks[block_type] : NULL;
}

/**
 * @brief Reject a block whose type nothing can run, at load.
 *
 * The one shape check there is. A type no function claims would otherwise load
 * fine and then be silently skipped on every pass -- a program that runs,
 * reports nothing and does less than it says. Called by the loader before the
 * block is built, so a rejected block costs the arena nothing.
 *
 * What a block *takes in* -- pin counts, private-state size, types -- is not
 * described anywhere yet. That belongs to load-time validation and to the
 * editor, and it arrives as its own table when there is a palette to describe.
 */
err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type);

/* ==========================================================================
   Time

   Two readings, and the distinction is the only thing worth saying about them:

     vm_clock_us()  live, read now
     vm_now_ms()    the stamp latched at the top of this pass

   **Timing blocks want vm_now_ms().** A pass takes real time to walk, so two
   blocks reading a live clock in the same pass get different answers -- and a
   gate compared against a moving clock can open for one branch of a merge and
   not the other, in the same pass, from the same enable. Latching once per pass
   makes a pass a single instant as far as the program is concerned, the same
   way the event buffer swap does for arrivals.

   vm_clock_us() is for measuring the machine rather than the program: pass
   duration, the block watchdog, anything whose job is to notice that time
   passed *within* a pass.

   64-bit, because a 32-bit millisecond counter wraps after 49 days -- well
   inside the uptime a machine controller is expected to reach -- and every
   comparison in every timing block would have to be written wrap-safe to
   survive it. esp_timer's counter is already this wide.
   ========================================================================== */

/** @brief Microseconds since boot, read live. */
static inline uint64_t vm_clock_us(void) {
  return (uint64_t)esp_timer_get_time();
}

/** @brief Latched at the top of every pass by the supervisor, which is the
 *  only writer. Read it through vm_now_ms(). */
extern uint64_t g_vm_pass_ms;

/** @brief What time it is, as far as this pass is concerned. The reading every
 *  timing block should use -- see above. */
static inline uint64_t vm_now_ms(void) {
  return g_vm_pass_ms;
}

/** @brief What the supervisor is doing between passes. */
typedef enum vm_run_mode_e {
  VM_RUN_STOPPED = 0,  // no pass runs; the task idles
  VM_RUN_RUNNING = 1,  // passes back to back
  VM_RUN_FROZEN = 2,   // held at the next freeze point
  /* Run exactly one scan, then hold. Step lands on VM_RUN_FROZEN when the
     pass completes, so stepping again is another vm_exec_set_mode(). */
  VM_RUN_STEP = 3,
} vm_run_mode_e;

/** @brief How deep spans may nest. A section comes off the wire and nothing
 *  in it stops a program declaring FORs inside FORs until the stack is gone --
 *  the same argument accessor chains have a depth cap for. */
#define VM_EXEC_MAX_SPAN_DEPTH 4

/** @brief How long one block may execute before the watchdog says it hung. */
#define VM_EXEC_BLOCK_WD_MS 20

/**
 * @brief Start the supervisor task, pinned to core 1. Idempotent.
 *
 * Starts in VM_RUN_STOPPED: a task that began running the instant it existed
 * would race the upload that gives it something to run.
 */
err_h vm_exec_start(void);

/** @brief Stop passing. The task stays alive and idle, so a later
 *  vm_exec_set_mode(VM_RUN_RUNNING) needs no re-creation. */
void vm_exec_stop(void);

void vm_exec_set_mode(vm_run_mode_e mode);
vm_run_mode_e vm_exec_mode(void);

/** @brief Passes completed since the last vm_exec_reset_stats(). */
uint32_t vm_exec_pass_count(void);

/** @brief How long the last pass took, in microseconds. The number the 10 ms
 *  floor is actually made of -- there is no tick behind it. */
uint32_t vm_exec_last_pass_us(void);

void vm_exec_reset_stats(void);

/**
 * @brief Run exactly one pass, on the calling task.
 *
 * What the supervisor loop calls, and public for two other callers: step mode,
 * and the self test, which needs a pass it can run synchronously and assert
 * about rather than one that happens on another core at some point.
 */
void vm_exec_pass(void);

/**
 * @brief Walk `[start, end)` of the block order once.
 *
 * Public because a span block is a supervisor of its own sub-range: FOR owns a
 * span and walks it N times within one pass, which is the one thing a flat
 * order cannot express. N must be bounded by the block itself so a section's
 * worst-case duration stays computable.
 *
 * Nesting is capped at VM_EXEC_MAX_SPAN_DEPTH; past that the range is skipped
 * and ERR_VM_EXEC_SPAN_DEPTH is raised.
 */
void vm_exec_run_range(uint16_t start, uint16_t end);

/**
 * @brief Install what samples live values at the end of each pass.
 *
 * A hook rather than a call into a subscription module, because none exists
 * yet and what does the subscribing is not the VM's to decide. What *is* the
 * VM's to decide is where in the pass it lands, and that is fixed: after the
 * sections, **before** `upd` is cleared. Sampling after the sweep would
 * silently break "send on update", which is that flag's other customer.
 *
 * Runs on the supervisor task, inside no section, so it sees a settled pass.
 * NULL to remove.
 */
void vm_exec_set_sample_hook(void (*hook)(void));

/**
 * @brief Drop everything the supervisor holds across a program.
 *
 * Called by the loader while holding the program lifecycle barrier, after
 * all active passes have returned. Clears events, watchdog state, clock, and
 * statistics; mode is selected when the caller releases the barrier.
 */
void vm_exec_reset(void);

/** @brief Lifecycle barrier for the control task, never a block/sample callback.
 * Prevent new passes and wait until the current pass has released its handles.
 * Return the prior mode, so failed replacement can resume the old program.
 * Pair with unlock; successful reset/open unlocks with VM_RUN_STOPPED. */
vm_run_mode_e vm_exec_program_lock(void);
void vm_exec_program_unlock(vm_run_mode_e mode);
