#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_timer.h"
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block.h"

/* ========================================================================= */
/* Constants & Limits                                                        */
/* ========================================================================= */

#define VM_EXEC_MAX_SPAN_DEPTH 4   // Maximum nested span depth (e.g. FOR loops)
#define VM_EXEC_BLOCK_WD_MS    20  // Max block execution duration before hang error

/* ========================================================================= */
/* Execution Modes & Control Commands                                        */
/* ========================================================================= */

typedef enum vm_run_mode_e {
  VM_RUN_STOPPED    = 0,  // Execution idle; passes do not run
  VM_RUN_RUNNING    = 1,  // Continuous cyclic passes (1 pass per tick)
  VM_RUN_FROZEN     = 2,  // Paused before next block dispatch
  VM_RUN_STEP       = 3,  // Run single pass, then transition to FROZEN
  VM_RUN_SCAN       = 4,  // Scan mode; waits for VM_EXEC_ONCE
  VM_RUN_BLOCK      = 5,  // Block mode; waits for VM_EXEC_NEXT
  VM_RUN_BLOCK_STEP = 6,  // Execute single pending block dispatch
} vm_run_mode_e;

typedef enum vm_exec_command_e {
  VM_EXEC_SCAN_MODE      = 0,
  VM_EXEC_ONCE           = 1,
  VM_EXEC_BLOCK_MODE     = 2,
  VM_EXEC_NEXT           = 3,
  VM_EXEC_RESET_TO_START = 4,
  VM_EXEC_NORMAL_MODE    = 5,
  VM_EXEC_PAUSE          = 6,
  VM_EXEC_RESUME         = 7,
  VM_EXEC_RESET          = 8,
} vm_exec_command_e;

typedef struct vm_exec_status_t {
  vm_run_mode_e mode;
  uint16_t      next_block;   // Target block ID if paused; UINT16_MAX otherwise
  bool          scan_active;  // Pass is currently executing
  bool          waiting;      // Core 1 task parked before next_block
} vm_exec_status_t;

/* ========================================================================= */
/* Palette Dispatch                                                          */
/* ========================================================================= */

extern const vm_block_fn g_vm_blocks[];
extern const uint16_t   g_vm_blocks_cnt;

/** @brief Resolve block function pointer from block_type; NULL if not in palette. */
static inline vm_block_fn vm_block_fn_for(uint8_t block_type) {
  return (block_type < g_vm_blocks_cnt) ? g_vm_blocks[block_type] : NULL;
}

/** @brief Validate block_type against the palette at load time. */
err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type);

/* ========================================================================= */
/* Clocks & Timestamps                                                       */
/* ========================================================================= */

extern uint64_t g_vm_pass_ms;  // Latched pass timestamp in ms

/** @brief Microseconds since boot, read live from timer. */
static inline uint64_t vm_clock_us(void) {
  return (uint64_t)esp_timer_get_time();
}

/** @brief Pass timestamp in ms, latched at top of current pass. Used by timing blocks. */
static inline uint64_t vm_now_ms(void) {
  return g_vm_pass_ms;
}

/* ========================================================================= */
/* Supervisor Task & Lifecycle                                               */
/* ========================================================================= */

/** @brief Start the supervisor FreeRTOS task on core 1. Idempotent. */
err_h vm_exec_start(void);

/** @brief Stop cyclic execution. Task idles in VM_RUN_STOPPED. */
void vm_exec_stop(void);

/** @brief Set run mode directly. */
void vm_exec_set_mode(vm_run_mode_e mode);

/** @brief Get current run mode. */
vm_run_mode_e vm_exec_mode(void);

/** @brief Execute interactive execution command (0x48 packet). */
err_h vm_exec_control(vm_exec_command_e command);

/** @brief Query current supervisor execution state and debug status. */
vm_exec_status_t vm_exec_status(void);

/** @brief True if current pass has been aborted by program reset/lock. */
bool vm_exec_cancelled(void);

/** @brief Lock execution barrier before program replacement; returns prior mode. */
vm_run_mode_e vm_exec_program_lock(void);

/** @brief Release execution barrier with new run mode. */
void vm_exec_program_unlock(vm_run_mode_e mode);

/** @brief Reset executor state, event queues, overrides, and statistics. */
void vm_exec_reset(void);

/* ========================================================================= */
/* Pass Execution & Metrics                                                  */
/* ========================================================================= */

/** @brief Execute a single pass across all loaded blocks. */
void vm_exec_pass(void);

/** @brief Execute a contiguous range of blocks [start, end). Used for spans/loops. */
void vm_exec_run_range(uint16_t start, uint16_t end);

/** @brief Set telemetry hook called at end of each pass before clearing upd flags. */
void vm_exec_set_sample_hook(void (*hook)(void));

/** @brief Completed passes count since last stats reset. */
uint32_t vm_exec_pass_count(void);

/** @brief Wall duration of the last pass in microseconds. */
uint32_t vm_exec_last_pass_us(void);

/** @brief Reset pass metrics. */
void vm_exec_reset_stats(void);
