#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_callbacks.h"
#include "sys_error.h"
#include "sys_error_vm.h"

/*
Events bridge system callbacks into the running VM pass:
  sys_callbacks (Core 0) -> [ s_vm_event_q ] -> vm_event_drain() -> [ s_cycle snapshot ] (Core 1)
Buffered as cb_event_t; valid for the duration of one scan cycle, lock-free to query.
*/

#define VM_EVENT_DEPTH 16

/** @brief Post an incoming callback event to the VM event queue (non-blocking). */
bool vm_event_post(const cb_event_t* ev);

/** @brief Number of events in current cycle snapshot. */
uint8_t vm_event_count(void);

/** @brief Direct pointer to this cycle's snapshot array. */
const cb_event_t* vm_event_snapshot(uint8_t* out_cnt);

/** @brief Event at index i of this cycle's snapshot, or NULL if out of bounds. */
const cb_event_t* vm_event_at(uint8_t i);

/** @brief Drain incoming queue into the current cycle snapshot (called at start of pass). */
void vm_event_drain(void);

/** @brief Fetch and clear any queue overflow error. */
err_h vm_event_take_overflow(void);

/** @brief Reset incoming queue and cycle snapshot. */
void vm_event_reset(void);
