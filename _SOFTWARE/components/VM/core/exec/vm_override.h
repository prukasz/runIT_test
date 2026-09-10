#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"

#define VM_OVERRIDE_MAX_DATA 32
#define VM_OVERRIDE_QUEUE_DEPTH 16

typedef struct {
  uint16_t id;
  uint16_t start_idx;
  uint16_t len;
  uint8_t data[VM_OVERRIDE_MAX_DATA];
} vm_override_record_t;

/**
 * @brief Enqueue a runtime variable update record (called from Core 0 decoder).
 * Validates object existence, user protection, mutability, and bounds before enqueueing.
 */
err_h vm_override_post(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len);

/**
 * @brief Drain and apply all pending runtime variable updates (called from Core 1 at scan boundary).
 * Copies data to object payload and sets upd = 1 so downstream blocks and telemetry react.
 */
void vm_override_drain(void);

/**
 * @brief Reset the override queue.
 */
void vm_override_reset(void);

/**
 * @brief Return the number of pending override records.
 */
uint16_t vm_override_pending_count(void);
