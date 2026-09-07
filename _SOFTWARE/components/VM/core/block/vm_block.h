#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vm_obj_access.h"

/*
Linear block layout:
  [cfg (16B)][in_cnt * const vm_accessor_t*][q_cnt * vm_obj_h][en_cnt * const vm_accessor_t*][custom_data bytes]
Inputs use accessors (NULL if unwired constant); outputs hold direct vm_obj_h.
*/

#define VM_BLOCK_MAX_IN  16
#define VM_BLOCK_MAX_OUT 16
#define VM_BLOCK_MAX_EN  16

/** @brief How a block combines its enable sources. */
#define VM_BLK_EN_ANY 0x00u  // OR / branch merge
#define VM_BLK_EN_ALL 0x01u  // AND / conditions

/** @brief What a failing block body does to the flow below it. */
#define VM_BLK_ERR_STOP     0x00u  // Publish false ENO, downstream self-skips
#define VM_BLK_ERR_CONTINUE 0x01u  // Report and carry on

/* Runtime flags (cfg.rt) latched per call (cleared before dispatch) or sticky. */
#define VM_BLK_RT_TRIGGERED (1u << 0u)  // Fresh data arrived on an input (latched by vm_block_triggered)
#define VM_BLK_RT_SPAN      (1u << 1u)  // Block took over execution range (vm_block_claim_span)
/* Sticky, and for any block type: the block's own custom_data is malformed for
   what its type expects. A standing condition -- the same bytes are wrong on
   every pass -- so the bit exists to report it once per load rather than at
   scan rate. The supervisor never reads it; the block that set it does. */
#define VM_BLK_RT_CFG_BAD   (1u << 6u)
#define VM_BLK_RT_SPAN_BAD  (1u << 7u)  // Sticky: malformed span reported once per load

#define VM_BLK_RT_PER_CALL (VM_BLK_RT_TRIGGERED | VM_BLK_RT_SPAN)

/** @brief Execution order span [start, end) for sections and loop blocks (e.g. FOR). */
typedef struct vm_span_t {
  uint16_t start;
  uint16_t end;
} vm_span_t;

typedef struct vm_block_data_t {
  struct {
    uint16_t block_idx;   // [0..1] Visual block identifier
    uint8_t  block_type;  // [2]    Palette type index
    uint8_t  in_cnt;      // [3]    Input count
    uint8_t  q_cnt;       // [4]    Output count
    uint8_t  en_cnt;      // [5]    Enable count (0 = root, always enabled)
    uint8_t  en_mode;     // [6]    VM_BLK_EN_ANY / _ALL
    uint8_t  on_error;    // [7]    VM_BLK_ERR_STOP / _CONTINUE
    uint16_t custom_len;  // [8..9] Private custom_data byte length
    uint8_t  rt;          // [10]   Runtime status bits (VM_BLK_RT_*)
    /* [11] 1 byte alignment padding before pointer */
    vm_obj_h eno;         // [12..15] Output ENO object (NULL if none)
  } cfg;  
  uint8_t data[];
} vm_block_data_t;

_Static_assert(sizeof(struct vm_block_data_t) == 16, "custom_len and rt must stay inside the alignment padding before `eno`");

typedef vm_block_data_t* vm_block_h;

/** @brief Block execution handler signature. Indexed by block_type in g_vm_blocks table. */
typedef void (*vm_block_fn)(vm_block_h);

/** @brief Block by ID from registry (NULL past end). */
static inline vm_block_h vm_block_by_id(uint16_t id) {
  return (vm_block_h)vm_store_get(VM_REG_BLK, id);
}

static inline const vm_accessor_t** vm_block_inputs(vm_block_h b) {
  return (const vm_accessor_t**)b->data;
}

static inline vm_obj_h* vm_block_outputs(vm_block_h b) {
  return (vm_obj_h*)(vm_block_inputs(b) + b->cfg.in_cnt);
}

/** @brief This block's enable sources -- en_cnt entries, empty for a root. */
static inline const vm_accessor_t** vm_block_en_list(vm_block_h b) {
  return (const vm_accessor_t**)(vm_block_outputs(b) + b->cfg.q_cnt);
}

static inline void* vm_block_custom_data(vm_block_h b) {
  return (void*)(vm_block_en_list(b) + b->cfg.en_cnt);
}

/** @brief Total allocation bytes required for one block of this shape. */
static inline size_t vm_block_size(uint8_t in_cnt, uint8_t q_cnt, uint8_t en_cnt, uint16_t custom_len) {
  return sizeof(vm_block_data_t) + (size_t)in_cnt * sizeof(const vm_accessor_t*) + (size_t)q_cnt * sizeof(vm_obj_h) + (size_t)en_cnt * sizeof(const vm_accessor_t*) + custom_len;
}

static inline uint16_t vm_block_custom_len(vm_block_h b) {
  return b->cfg.custom_len;
}

static inline size_t vm_block_total_size(vm_block_h b) {
  return vm_block_size(b->cfg.in_cnt, b->cfg.q_cnt, b->cfg.en_cnt, b->cfg.custom_len);
}

/* The pin builders moved to core/errors/vm_errors.h with every other cold
   error arm; vm_obj_access.h above pulls it in. This one stays: it reports
   rather than builds. */
void vm_block_report_error(err_h cause, uint16_t block_idx, uint8_t block_type);

/** @brief Latches failure reported by the executing block body; inspected by supervisor for on_error policy. */
extern bool g_vm_block_fault;

/** @brief True if any input carries fresh data (latches VM_BLK_RT_TRIGGERED). */
bool vm_block_triggered(vm_block_h b);

/** @brief True if specific input pin carries fresh data. */
bool vm_block_input_fresh(vm_block_h b, uint8_t pin);

/** @brief Block's execution span from custom_data (NULL if custom_len < sizeof(vm_span_t)). */
static inline const vm_span_t* vm_block_span(vm_block_h b) {
  if (b->cfg.custom_len < sizeof(vm_span_t)) return NULL;
  return (const vm_span_t*)vm_block_custom_data(b);
}

/** @brief Takes over [start, end) range so outer execution walk jumps over it. */
void vm_block_claim_span(vm_block_h b, uint16_t start, uint16_t end);

/** @brief Fetch input pin accessor (ERR_VM_BLOCK_PIN_MISSING or ERR_VM_BLOCK_PIN_UNLINKED if NULL). */
static inline err_h vm_block_get_in(const vm_accessor_t** target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.in_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, false);
  const vm_accessor_t* acc = vm_block_inputs(b)[id];
  if (unlikely(!acc)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, false);
  *target = acc;
  return NULL;
}

/** @brief Fetch output pin object (ERR_VM_BLOCK_PIN_MISSING or ERR_VM_BLOCK_PIN_UNLINKED if NULL). */
static inline err_h vm_block_get_out(vm_obj_h* target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.q_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, true);
  vm_obj_h obj = vm_block_outputs(b)[id];
  if (unlikely(!obj)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, true);
  *target = obj;
  return NULL;
}

/** @brief Evaluates block enable status across en_cnt sources (0 = always enabled). */
static inline bool vm_block_is_enabled(vm_block_h b) {
  uint8_t n = b->cfg.en_cnt;
  if (n == 0) return true;

  const vm_accessor_t** en = vm_block_en_list(b);
  const bool all = (b->cfg.en_mode == VM_BLK_EN_ALL);
  for (uint8_t i = 0; i < n; i++) {
    bool v = false;
    err_h e = VM_OBJ_GET_VAL(v, en[i]);
    if (unlikely(e)) {
      vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      v = false;  // fail closed
    }
    if (all) {
      if (!v) return false;
    } else if (v) {
      return true;
    }
  }
  return all;
}

/* Block activation macros. Nestable: IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) { ... } */
#define IF_BLOCK_ENABLED(block) if (vm_block_is_enabled(block))
#define IF_BLOCK_TRIGGERED(block) if (vm_block_triggered(block))

/** @brief Sets ENO: true sets upd (loud news), false clears quietly without setting upd. */
static inline void vm_block_set_ENO(vm_block_h b, bool state) {
  if (!b->cfg.eno) return;
  if (!state) {
    vm_obj_clear_quiet(b->cfg.eno);
    return;
  }
  uint8_t v = 1;
  err_h e = VM_OBJ_SET_VAL_AT(v, b->cfg.eno, 0);
  if (unlikely(e)) {
    vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  }
}

/** @brief Runs an err_h call, reporting failure with block context and latching g_vm_block_fault. */
#define BLOCK_CALL(call, block)                                                                                                                                       \
  do {                                                                                                                                                                \
    err_h __bc_e = (call);                                                                                                                                            \
    if (__bc_e) {                                                                                                                                                     \
      g_vm_block_fault = true;                                                                                                                                        \
      SE_push_to_handler(SE_WRAP_ERR_OWNED(OWNER_VM_BLOCK, __bc_e, ERR_VM_BLOCK_FAILED, .block_idx = (block)->cfg.block_idx, .block_type = (block)->cfg.block_type)); \
    }                                                                                                                                                                 \
  } while (0)

#define VM_BLK_ERR_NEW(tag_name, ...)                                                               \
  ({                                                                                                 \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, OWNER_VM_BLOCK);         \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};        \
    __e;                                                                                             \
  })

#define VM_BLK_EMIT_ERR(tag_name, ...) SE_push_to_handler(VM_BLK_ERR_NEW(tag_name, __VA_ARGS__))
