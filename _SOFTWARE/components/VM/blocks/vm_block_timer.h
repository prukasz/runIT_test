#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_compiler.h"
#include "vm_block_support.h"
#include "vm_exec.h"

/*
 * Timer Block (Flow Control / Timing: TON, TOF, TP + Inverted)
 *
 * Inputs:
 *   - in[0]: IN trigger / gate signal (VM_OBJ_B or any non-zero numeric scalar).
 *   - in[1]: Optional dynamic preset time PT in ms. When omitted or unwired,
 *            falls back to hardcoded `pt_ms` in `custom_data`.
 *
 * Outputs:
 *   - out[0]: Optional Q output (boolean, follows timer state, inverted if mode/flag set).
 *   - out[1]: Optional ET output (elapsed time dt in ms, converted to output object type).
 *   - ENO:    Flow control level (set to Q to enable direct downstream gating in DAG).
 *
 * Modes:
 *   - VM_TIMER_TON:     On-Delay. When IN=true, delays turning Q=true for PT ms.
 *   - VM_TIMER_TOF:     Off-Delay. When IN falls to false, keeps Q=true for PT ms.
 *   - VM_TIMER_TP:      Pulse Timer. When IN rises (0->1), pulses Q=true for PT ms.
 *   - VM_TIMER_TON_INV: Inverted On-Delay (!Q).
 *   - VM_TIMER_TOF_INV: Inverted Off-Delay (!Q).
 *   - VM_TIMER_TP_INV:  Inverted Pulse Timer (!Q).
 *
 * Memory Layout in custom_data:
 *   [mode (1B)][flags (1B)][_pad1 (2B)][_pad2 (4B)][pt_ms (8B)][start_ms (8B)][elapsed_ms (8B)] = 32B
 */

typedef enum {
  VM_TIMER_TON     = 0,  // On-Delay
  VM_TIMER_TOF     = 1,  // Off-Delay
  VM_TIMER_TP      = 2,  // Pulse Timer
  VM_TIMER_TON_INV = 3,  // Inverted On-Delay (!Q)
  VM_TIMER_TOF_INV = 4,  // Inverted Off-Delay (!Q)
  VM_TIMER_TP_INV  = 5,  // Inverted Pulse Timer (!Q)
  VM_TIMER_MODE_CNT = 6,
} vm_timer_mode_e;

#define VM_TIMER_F_INITIALIZED (1u << 0)
#define VM_TIMER_F_RUNNING     (1u << 1)
#define VM_TIMER_F_PREV_IN     (1u << 2)
#define VM_TIMER_F_INVERTED    (1u << 3)

typedef struct __attribute__((aligned(8))) {
  uint8_t  mode;        // vm_timer_mode_e
  uint8_t  flags;       // VM_TIMER_F_*
  uint16_t _pad1;
  uint32_t _pad2;
  uint64_t pt_ms;       // Preset time in ms (hardcoded fallback)
  uint64_t start_ms;    // Timestamp when timing started (from vm_now_ms())
  uint64_t elapsed_ms;  // Current elapsed time dt in ms
} vm_block_timer_data_t;

_Static_assert(sizeof(vm_block_timer_data_t) == 32, "vm_block_timer_data_t must be 32 bytes");

/**
 * @brief Initialize timer configuration in block custom data.
 */
static inline void vm_block_timer_init_data(void* buffer, vm_timer_mode_e mode, uint64_t pt_ms, bool inverted) {
  const vm_block_timer_data_t data = {
      .mode = (uint8_t)mode, .pt_ms = pt_ms,
      .flags = inverted ? VM_TIMER_F_INVERTED : 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

#define VM_TIMER_IN_SIGNAL 0u
#define VM_TIMER_IN_PT 1u
#define VM_TIMER_Q 0u
#define VM_TIMER_ET 1u

static inline bool vm_timer_elapsed(vm_block_timer_data_t* d, uint64_t pt, uint64_t now) {
  const uint64_t elapsed = now >= d->start_ms ? now - d->start_ms : 0;
  d->elapsed_ms = elapsed >= pt ? pt : elapsed;
  return elapsed >= pt;
}

static inline void vm_timer_start(vm_block_timer_data_t* d, uint64_t now) {
  d->flags |= VM_TIMER_F_RUNNING;
  d->start_ms = now;
  d->elapsed_ms = 0;
}

/* Pure state transition: no accessors, outputs, clock reads, or diagnostics. */
static inline bool vm_timer_step(vm_block_timer_data_t* d, bool in_val, uint64_t pt, uint64_t now) {
  const bool first_scan = !(d->flags & VM_TIMER_F_INITIALIZED);
  const bool prev_in = (d->flags & VM_TIMER_F_PREV_IN) != 0;
  bool q = false;

  // Preserve wire aliases: an inverted mode and the inversion flag combine by XOR.
  const uint8_t base_mode = d->mode % 3u;
  const bool invert = (d->mode >= VM_TIMER_TON_INV) != ((d->flags & VM_TIMER_F_INVERTED) != 0);

  switch (base_mode) {
    case VM_TIMER_TON: {
      if (in_val) {
        if (first_scan || !(d->flags & VM_TIMER_F_RUNNING)) {
          vm_timer_start(d, now);
        } else {
          (void)vm_timer_elapsed(d, pt, now);
        }
        q = (d->elapsed_ms >= pt);
      } else {
        d->flags &= ~VM_TIMER_F_RUNNING;
        d->elapsed_ms = 0;
        q = false;
      }
      break;
    }

    case VM_TIMER_TOF: {
      if (in_val) {
        d->flags &= ~VM_TIMER_F_RUNNING;
        d->elapsed_ms = 0;
        q = true;
      } else {
        if (!first_scan && prev_in) {
          // 1 -> 0 transition: start off-delay timing
          vm_timer_start(d, now);
          q = (pt > 0);
        } else if (d->flags & VM_TIMER_F_RUNNING) {
          if (vm_timer_elapsed(d, pt, now)) {
            d->flags &= ~VM_TIMER_F_RUNNING;
            q = false;
          } else {
            q = true;
          }
        } else {
          d->elapsed_ms = pt;
          q = false;
        }
      }
      break;
    }

    case VM_TIMER_TP: {
      if (!first_scan && !prev_in && in_val && !(d->flags & VM_TIMER_F_RUNNING)) {
        // 0 -> 1 rising edge trigger
        vm_timer_start(d, now);
        q = (pt > 0);
      } else if (d->flags & VM_TIMER_F_RUNNING) {
        if (vm_timer_elapsed(d, pt, now)) {
          d->flags &= ~VM_TIMER_F_RUNNING;
          q = false;
        } else {
          q = true;
        }
      } else {
        d->elapsed_ms = 0;
        q = false;
      }
      break;
    }

    default:
      break;
  }

  // Record history
  d->flags |= VM_TIMER_F_INITIALIZED;
  if (in_val) {
    d->flags |= VM_TIMER_F_PREV_IN;
  } else {
    d->flags &= ~VM_TIMER_F_PREV_IN;
  }

  // Apply optional inversion
  if (invert) {
    q = !q;
  }

  return q;
}

/* Enable-driven. Disabled timers reset; Q/ET are value writes, ENO follows Q. */
static inline void vm_blk_timer(vm_block_h b) {
  if (unlikely(b->cfg.custom_len < sizeof(vm_block_timer_data_t))) {
    vm_block_cfg_bad(b);
    vm_block_set_eno(b, false);
    return;
  }
  vm_block_timer_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  if (unlikely(state.mode >= VM_TIMER_MODE_CNT)) {
    vm_block_cfg_bad(b);
    vm_block_set_eno(b, false);
    return;
  }
  const bool valid = vm_block_require(b, 1, 0, 0x1u);
  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~(VM_TIMER_F_RUNNING | VM_TIMER_F_INITIALIZED | VM_TIMER_F_PREV_IN);
    state.elapsed_ms = 0;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_TIMER_Q, false);
    vm_block_drive_gate(b, VM_TIMER_ET, false);
    vm_block_set_eno(b, false);
    return;
  }
  bool signal = false;
  uint64_t pt = 0;
  if (!valid || !vm_block_read_bool(&signal, b, vm_block_get_inputs(b)[VM_TIMER_IN_SIGNAL]) ||
      !vm_block_param_u64(&pt, b, VM_TIMER_IN_PT, state.pt_ms)) {
    vm_block_set_eno(b, false);
    return;
  }
  uint64_t now = vm_now_ms();
  if (unlikely(now == 0)) now = vm_clock_us() / 1000;
  const bool q = vm_timer_step(&state, signal, pt, now);
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, q);
  if (b->cfg.q_cnt > VM_TIMER_Q) {
    uint8_t value = q ? 1 : 0;
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(value, vm_block_get_outputs(b)[VM_TIMER_Q], 0), b);
  }
  if (b->cfg.q_cnt > VM_TIMER_ET) {
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(state.elapsed_ms, vm_block_get_outputs(b)[VM_TIMER_ET], 0), b);
  }
}
