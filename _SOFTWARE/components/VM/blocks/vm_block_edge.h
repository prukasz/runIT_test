#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_compiler.h"
#include "vm_block_support.h"

/*
 * Edge Detection Block (Flow Control)
 *
 * Inputs:
 *   - in[0]: Monitored signal / value (VM_OBJ_B, VM_OBJ_F, VM_OBJ_I32, VM_OBJ_U8, VM_OBJ_U32, VM_OBJ_U64).
 *   - in[1]: Optional dynamic hysteresis / threshold. When omitted or unwired,
 *            falls back to hardcoded `change_by` configured in `custom_data`.
 *
 * Outputs:
 *   - out[0]: Optional pulse output (driven to 1 for exactly 1 pass, then cleared).
 *   - ENO:    Flow control pulse (set to true for exactly 1 pass, then false).
 *
 * Modes:
 *   - VM_EDGE_RISING:  Low -> High, or scalar increase (>= threshold)
 *   - VM_EDGE_FALLING: High -> Low, or scalar decrease (>= threshold)
 *   - VM_EDGE_BOTH:    Any state toggle, or scalar change (|delta| >= threshold)
 *
 * Memory Layout in custom_data:
 *   [edge_type (1B)][flags (1B)][padding (6B)][change_by (8B)][prev_val (8B)] = 24B
 */

typedef enum {
  VM_EDGE_RISING  = 0,  // 0 -> 1, or increase by >= threshold
  VM_EDGE_FALLING = 1,  // 1 -> 0, or decrease by >= threshold
  VM_EDGE_BOTH    = 2,  // Any toggle, or |delta| >= threshold
  VM_EDGE_TYPE_CNT = 3,
} vm_edge_type_e;

#define VM_EDGE_F_INITIALIZED (1u << 0)  // Previous value is recorded

typedef union {
  uint64_t u;
  int64_t  i;
  float    f;
} vm_edge_val_u;

typedef struct __attribute__((aligned(8))) {
  uint8_t       edge_type;  // vm_edge_type_e
  uint8_t       flags;      // VM_EDGE_F_*
  uint16_t      _pad1;
  uint32_t      _pad2;
  vm_edge_val_u change_by;  // Threshold for change condition (0 = any change)
  vm_edge_val_u prev_val;   // Stored previous value (max uint64_t)
} vm_block_edge_data_t;

_Static_assert(sizeof(vm_block_edge_data_t) == 24, "vm_block_edge_data_t must be 24 bytes");
#define VM_EDGE_CUSTOM_LEN sizeof(vm_block_edge_data_t)

/**
 * @brief Initialize edge detection configuration in block custom data.
 */
static inline void vm_edge_init(void* buffer, vm_edge_type_e type, vm_edge_val_u threshold) {
  const vm_block_edge_data_t data = {.edge_type = (uint8_t)type, .change_by = threshold};
  memcpy(buffer, &data, sizeof(data));
}

/* Compatibility initializer. New callers select the signal domain explicitly
 * with vm_edge_init(buffer, mode, (vm_edge_val_u){.f/.i/.u = threshold}). */
static inline void vm_block_edge_init_data(void* buffer, vm_edge_type_e type, float change_f, uint64_t change_u) {
  vm_edge_val_u threshold = {.f = change_f};
  if (change_u != 0) threshold.u = change_u;
  vm_edge_init(buffer, type, threshold);
}

#define VM_EDGE_IN_SIGNAL 0u
#define VM_EDGE_IN_THRESHOLD 1u
#define VM_EDGE_Q 0u

static inline bool vm_edge_direction(uint8_t mode, bool rising, bool falling) {
  return mode == VM_EDGE_RISING ? rising : mode == VM_EDGE_FALLING ? falling : rising || falling;
}

/* Sample once, compare in the signal's numeric domain, then record history.
 * Unsigned deltas are formed only in the direction that cannot underflow. */
static inline err_h vm_edge_step(vm_block_edge_data_t* d, vm_payload_t p, vm_edge_val_u th, bool* out) {
  vm_edge_val_u curr = {0};
  switch (p.type) {
    case VM_OBJ_B: curr.u = *(const uint8_t*)p.ptr != 0; break;
    case VM_OBJ_F: curr.f = *(const float*)p.ptr; break;
    case VM_OBJ_I32: curr.i = *(const int32_t*)p.ptr; break;
    case VM_OBJ_U8: curr.u = *(const uint8_t*)p.ptr; break;
    case VM_OBJ_U32: curr.u = *(const uint32_t*)p.ptr; break;
    case VM_OBJ_U64: curr.u = vm_internal_get_u64(p.ptr); break;
    default: return VM_BLK_ERR_NEW(ERR_VM_OBJ_BAD_TYPE, .type = p.type);
  }
  *out = false;
  if (d->flags & VM_EDGE_F_INITIALIZED) {
    bool rising, falling;
    if (p.type == VM_OBJ_B) {
      rising = !d->prev_val.u && curr.u;
      falling = d->prev_val.u && !curr.u;
    } else if (p.type == VM_OBJ_F) {
      rising = th.f > 0.0f ? curr.f - d->prev_val.f >= th.f : curr.f > d->prev_val.f;
      falling = th.f > 0.0f ? d->prev_val.f - curr.f >= th.f : curr.f < d->prev_val.f;
      // Preserve any-change semantics for NaN when no positive threshold is set.
      if (!(th.f > 0.0f) && d->edge_type == VM_EDGE_BOTH) {
        rising = curr.f != d->prev_val.f;
      }
    } else if (p.type == VM_OBJ_I32) {
      const int64_t diff = curr.i - d->prev_val.i;
      rising = th.i > 0 ? diff >= th.i : diff > 0;
      falling = th.i > 0 ? -diff >= th.i : diff < 0;
    } else {
      rising = curr.u > d->prev_val.u && curr.u - d->prev_val.u >= th.u;
      falling = d->prev_val.u > curr.u && d->prev_val.u - curr.u >= th.u;
    }
    *out = vm_edge_direction(d->edge_type, rising, falling);
  }
  d->prev_val = curr;
  d->flags |= VM_EDGE_F_INITIALIZED;
  return NULL;
}

static inline bool vm_verify_edge(vm_block_h b) {
  if (b->cfg.custom_len < sizeof(vm_block_edge_data_t)) return false;
  if (!vm_block_require(b, 1, 0, 0x1u)) return false;
  const vm_block_edge_data_t* d = (const vm_block_edge_data_t*)vm_block_get_custom_data(b);
  if (d->edge_type >= VM_EDGE_TYPE_CNT) return false;
  return true;
}

/* Enable-driven. First sample seeds history; disabled clears history and pulse.
 * A pulse is loud only when true, matching ENO and branch gate semantics. */
static inline void vm_blk_edge(vm_block_h b) {
  vm_block_edge_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~VM_EDGE_F_INITIALIZED;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_EDGE_Q, false);
    vm_block_set_eno(b, false);
    return;
  }
  vm_payload_t signal;
  if (!vm_block_check(b, vm_obj_get_payload(&signal, vm_block_get_inputs(b)[VM_EDGE_IN_SIGNAL]))) {
    vm_block_set_eno(b, false);
    return;
  }
  vm_edge_val_u threshold = state.change_by;
  bool read;
  if (signal.type == VM_OBJ_F) {
    read = VM_BLOCK_GET_PARAM(threshold.f, b, VM_EDGE_IN_THRESHOLD, state.change_by.f);
  } else if (signal.type == VM_OBJ_I32) {
    read = VM_BLOCK_GET_PARAM(threshold.i, b, VM_EDGE_IN_THRESHOLD, state.change_by.i);
  } else {
    read = VM_BLOCK_GET_PARAM(threshold.u, b, VM_EDGE_IN_THRESHOLD, state.change_by.u);
  }
  bool fired = false;
  if (!read || !vm_block_check(b, vm_edge_step(&state, signal, threshold, &fired))) {
    vm_block_set_eno(b, false);
    return;
  }
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, fired);
  vm_block_drive_gate(b, VM_EDGE_Q, fired);
}
