#pragma once
#include "vm_block.h"

/* Shared block plumbing. Configuration faults are sticky diagnostics, but
 * remain execution faults on every invocation. Helpers never choose ENO. */
static inline bool vm_block_check(vm_block_h b, err_h e) {
  if (!e) return true;
  vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  return false;
}

static inline bool vm_block_cfg_bad(vm_block_h b) {
  g_vm_block_fault = true;
  if (!(b->cfg.rt & VM_BLK_RT_CFG_BAD)) {
    b->cfg.rt |= VM_BLK_RT_CFG_BAD;
    vm_block_report_error(VM_BLK_ERR_NEW(ERR_VM_BLK_BAD_SHAPE,
        .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt, .q_cnt = b->cfg.q_cnt),
        b->cfg.block_idx, b->cfg.block_type);
  }
  return false;
}

static inline bool vm_block_require(vm_block_h b, uint8_t min_in, uint8_t min_q, uint16_t required_inputs) {
  if (b->cfg.in_cnt < min_in || b->cfg.q_cnt < min_q) return vm_block_cfg_bad(b);
  for (uint8_t pin = 0; pin < VM_BLOCK_MAX_IN; ++pin) {
    if (!(required_inputs & (1u << pin))) continue;
    if (pin >= b->cfg.in_cnt) return vm_block_cfg_bad(b);
    if (vm_block_get_inputs(b)[pin]) continue;
    g_vm_block_fault = true;
    if (!(b->cfg.rt & VM_BLK_RT_CFG_BAD)) {
      b->cfg.rt |= VM_BLK_RT_CFG_BAD;
      vm_block_report_error(vm_block_err_pin_unlinked(b->cfg.block_idx, pin, false),
                            b->cfg.block_idx, b->cfg.block_type);
    }
    return false;
  }
  return true;
}

/* Absence is a valid fallback, never an error-producing required-pin lookup. */
static inline const vm_accessor_t* vm_block_optional_in(vm_block_h b, uint8_t pin) {
  return pin < b->cfg.in_cnt ? vm_block_get_inputs(b)[pin] : NULL;
}

#define VM_BLOCK_PARAM_READER(suffix, type)                                      \
  static inline bool vm_block_param_##suffix(type* out, vm_block_h b,            \
                                             uint8_t pin, type fallback) {      \
    *out = fallback;                                                            \
    const vm_accessor_t* acc = vm_block_optional_in(b, pin);                     \
    return !acc || vm_block_check(b, VM_OBJ_GET_VAL(*out, acc));                  \
  }
VM_BLOCK_PARAM_READER(f32, float)
VM_BLOCK_PARAM_READER(i64, int64_t)
VM_BLOCK_PARAM_READER(u64, uint64_t)
#undef VM_BLOCK_PARAM_READER

/* Numeric truth preserves fractional floats and every bit of unsigned values. */
static inline bool vm_block_read_bool(bool* out, vm_block_h b, const vm_accessor_t* acc) {
  vm_payload_t p;
  if (!vm_block_check(b, vm_obj_get_payload(&p, acc))) return false;
  switch (p.type) {
    case VM_OBJ_B:
    case VM_OBJ_U8: *out = *(const uint8_t*)p.ptr != 0; return true;
    case VM_OBJ_I32: *out = *(const int32_t*)p.ptr != 0; return true;
    case VM_OBJ_U32: *out = *(const uint32_t*)p.ptr != 0; return true;
    case VM_OBJ_U64: *out = vm_internal_get_u64(p.ptr) != 0; return true;
    case VM_OBJ_F: *out = *(const float*)p.ptr != 0.0f; return true;
    default: return vm_block_check(b, VM_BLK_ERR_NEW(ERR_VM_OBJ_BAD_TYPE, .type = p.type));
  }
}

/* Gate false is quiet; ordinary value writes (including false) remain loud. */
static inline void vm_block_drive_gate(vm_block_h b, uint8_t pin, bool state) {
  if (pin >= b->cfg.q_cnt) return;
  vm_obj_h q = vm_block_get_outputs(b)[pin];
  if (!state) vm_obj_clear_quiet(q);
  else {
    uint8_t one = 1;
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(one, q, 0), b);
  }
}
