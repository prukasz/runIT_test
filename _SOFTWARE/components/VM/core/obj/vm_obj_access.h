#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_errors.h"
#include "vm_obj.h"
#include "vm_store.h"

/*
Object Access & Resolution Layer:
  - Fast-path (inline): vm_resolve_fast() resolves cached, root, or shallow indexed accessors.
  - Slow-path (out-of-line): resolve_d() walks multi-tier and dynamic by-ref/by-name chains.
  - Fast-read: VM_OBJ_GET_VAL() loads & converts inlined/shared.
  - Fast-write: VM_OBJ_SET_VAL() / VM_OBJ_SET_VAL_AT() stores directly with upd flag latching.
*/

static __always_inline vm_obj_h vm_obj_by_id(uint16_t id) {
  return (vm_obj_h)vm_store_get(VM_REG_OBJ, id);
}

/** @brief Where a value lives: address in arena, element count, type. (8 bytes). */
typedef struct vm_payload_t {
  void* ptr;       // NULL when unresolved (4 bytes)
  uint16_t count;  // number of `type` elements available at ptr (2 bytes)
  uint8_t type;    // vm_obj_t_e (1 byte)
  uint8_t _pad;    // (1 byte)
} vm_payload_t;

_Static_assert(sizeof(vm_payload_t) == 8, "vm_payload_t must be 8 bytes");

/** @brief Scratch value wide enough for any scalar vm_obj_t_e. */
typedef union {
  uint8_t u8;
  uint32_t u32;
  int32_t i32;
  uint64_t u64;
  float f;
} vm_val_t;

/** @brief Element `i` of an already-resolved payload, bounds-checked. */
static __always_inline vm_payload_t vm_payload_at(vm_payload_t p, uint16_t i) {
  if (unlikely(!vm_type_ok((uint8_t)p.type) || i >= p.count)) {
    return (vm_payload_t){.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  }
  return (vm_payload_t){
      .ptr = (uint8_t*)p.ptr + ((size_t)i << vm_type_shift((uint8_t)p.type)),
      .count = 1,
      .type = p.type,
      ._pad = 0,
  };
}

/** @brief A whole object as a payload. */
static __always_inline vm_payload_t vm_obj_as_payload(vm_obj_h obj) {
  return (vm_payload_t){
      .ptr = obj->payload,
      .count = vm_obj_items_cnt(obj),
      .type = (uint8_t)obj->head.d.obj_t,
      ._pad = 0,
  };
}

// ---------------------------------------------------------------------------
// Accessors & Index Chains
// ---------------------------------------------------------------------------

typedef struct vm_accessor_t vm_accessor_t;

typedef enum vm_index_kind_e {
  VM_IDX_LITERAL = 0,  // fixed position, known at compile time
  VM_IDX_REF = 1,      // resolve another accessor and read it as the index
  VM_IDX_NAME = 2,     // match a child object's tag
} vm_index_kind_e;

typedef struct {
  uint8_t kind;      // vm_index_kind_e
  uint8_t name_len;  // VM_IDX_NAME only: strlen(name), measured once at build
  union {
    uint32_t value;            // VM_IDX_LITERAL
    const vm_accessor_t* ref;  // VM_IDX_REF
    const char* name;          // VM_IDX_NAME
  };
} vm_index_t;

#define VM_IDX_BY_NAME(str) {.kind = VM_IDX_NAME, .name_len = (uint8_t)(sizeof(str) - 1), .name = (str)}

#define VM_ACC_F_CACHED 0x01u

struct vm_accessor_t {
  uint16_t id;                // root object's id in registry
  uint8_t count;              // number of chained indices; 0 = whole object
  uint8_t flags;              // VM_ACC_F_*
  const vm_index_t* indices;  // `count` entries allocated in trailing chunk
  vm_obj_h c_owner;           // cache: owning object
  vm_payload_t c_payload;     // cache: resolved payload (ptr, count, type, pad)
};

_Static_assert(sizeof(struct vm_accessor_t) == 20, "accessor header size feeds the RAM budget");

static __always_inline vm_accessor_t* vm_accessor_by_id(uint16_t id) {
  return (vm_accessor_t*)vm_store_get(VM_REG_ACC, id);
}

#define VM_ACCESSOR_MAX_DEPTH 8

// ---------------------------------------------------------------------------
// Resolution Fast-Path
// ---------------------------------------------------------------------------

typedef struct vm_resolved_t {
  vm_payload_t payload;
  vm_obj_h owner;  // object the payload's bytes belong to
} vm_resolved_t;

/**
 * @brief Fast inline resolution for cached or shallow index accessors.
 * Returns false on miss or failure so caller falls back to out-of-line resolve_d().
 */
static __always_inline bool vm_resolve_fast(const vm_accessor_t* acc, bool for_write, vm_resolved_t* out) {
  if (likely(acc->flags & VM_ACC_F_CACHED)) {
    if (unlikely(for_write && !acc->c_owner->head.f.mutable)) return false;
    out->payload = acc->c_payload;
    out->owner = acc->c_owner;
    return true;
  }

  vm_obj_h obj = vm_obj_by_id(acc->id);
  if (unlikely(obj == NULL)) return false;

  uint8_t n = acc->count;
  if (unlikely(n > 2)) return false;

  if (n == 0) {
    if (unlikely(for_write && !obj->head.f.mutable)) return false;
    out->payload = vm_obj_as_payload(obj);
    out->owner = obj;
    return true;
  }

  const vm_index_t* idx = acc->indices;
  if (unlikely(idx == NULL || idx[0].kind != VM_IDX_LITERAL)) return false;

  if (n == 2) {
    if (unlikely(idx[1].kind != VM_IDX_LITERAL || (uint8_t)obj->head.d.obj_t != VM_OBJ_PTR)) return false;
    uint32_t off = idx[0].value << 2;
    if (unlikely(off >= obj->head.payload_size)) return false;
    obj = *(vm_obj_h*)(obj->payload + off);
    if (unlikely(obj == NULL)) return false;
  }

  if (unlikely(for_write && !obj->head.f.mutable)) return false;

  uint8_t* p = vm_obj_elem_ptr(obj, idx[n - 1].value);
  if (unlikely(p == NULL)) return false;

  out->payload = (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
  out->owner = obj;
  return true;
}

// ---------------------------------------------------------------------------
// Out-of-Line Entry Points (vm_obj_access.c)
// ---------------------------------------------------------------------------

err_h vm_obj_get_payload(vm_payload_t* target, const vm_accessor_t* source);
err_h vm_get_obj(vm_obj_h* target, const vm_accessor_t* source);
vm_obj_h vm_obj_find_child(vm_obj_h parent, const char* tag);
/* How deep a copy will follow a pointer tree, and why there is a cap at all:
   a tree assembled from a packet is not guaranteed acyclic, so the bound is
   what turns a cycle into an error instead of a stack overflow. Same reason
   and same depth as VM_ACCESSOR_MAX_DEPTH. */
#define VM_OBJ_COPY_MAX_DEPTH 8

/* `reason` in ERR_VM_OBJ_COPY_SHAPE -- kept in step with VM_COPY_SHAPE_NAME()
   in sys_error_vm.h, which renders them. */
#define VM_COPY_SHAPE_DEPTH 0u      // ran out of depth, or the tree loops
#define VM_COPY_SHAPE_SRC_EMPTY 1u  // source slot unwired, target holds an object
#define VM_COPY_SHAPE_DST_EMPTY 2u  // target slot unwired, source holds an object

err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);

/** @brief Do these two trees have the same shape -- same type and element
 *  count at every level, and the same wired/unwired slots? True means
 *  vm_obj_copy_content() can move values between them without building
 *  anything, which is what lets a Clone allocate only when the shape changed. */
bool vm_obj_shape_matches(vm_obj_h a, vm_obj_h b);

/** @brief Build a dynamic tree shaped like `src`, values left zero.
 *  Reference count zero -- owned by nothing until a pointer slot takes it, so
 *  link it in the same call or release it. Tags are carried over, since a
 *  by-name accessor onto the copy has to keep working. */
err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src);

/** @brief Copy `source` into the pointer cell `target` names, building the
 *  destination first if what is there does not match. Allocates only on a
 *  shape change; steady state is the same walk vm_obj_copy_content() does. */
err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target);
err_h vm_obj_link(const vm_accessor_t* to_join, const vm_accessor_t* owner);
err_h vm_obj_set_scalar(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type);
void vm_obj_clear_quiet(vm_obj_h obj);

static __always_inline err_h vm_obj_set_scalar_direct(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type);
err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child);

// ---------------------------------------------------------------------------
// Value Conversion
// ---------------------------------------------------------------------------

static __always_inline vm_val_t vm_payload_read(vm_payload_t p) {
  vm_val_t v = {0};
  if (!p.ptr) return v;
  switch (p.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      v.u8 = *(uint8_t*)p.ptr;
      break;
    case VM_OBJ_U32:
      v.u32 = *(uint32_t*)p.ptr;
      break;
    case VM_OBJ_I32:
      v.i32 = *(int32_t*)p.ptr;
      break;
    case VM_OBJ_U64:
      memcpy(&v.u64, p.ptr, sizeof(v.u64));
      break;
    case VM_OBJ_F:
      v.f = *(float*)p.ptr;
      break;
    default:
      break;
  }
  return v;
}

static __always_inline int64_t vm_f_to_i(float f) {
  if (isnan(f)) return 0;
  if (f >= 9.2233715e18f) return INT64_MAX;
  if (f <= -9.2233715e18f) return INT64_MIN;
  return (int64_t)roundf(f);
}

#define VM_VAL_CAST_TO(dst_ptr, val, type)                                                                                       \
  do {                                                                                                                           \
    switch (type) {                                                                                                              \
      case VM_OBJ_U8:                                                                                                            \
      case VM_OBJ_B:                                                                                                             \
      case VM_OBJ_STR:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u8;                                                                           \
        break;                                                                                                                   \
      case VM_OBJ_U32:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u32;                                                                          \
        break;                                                                                                                   \
      case VM_OBJ_I32:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).i32;                                                                          \
        break;                                                                                                                   \
      case VM_OBJ_U64:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u64;                                                                          \
        break;                                                                                                                   \
      case VM_OBJ_F:                                                                                                             \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr), float: (val).f, double: (val).f, default: vm_f_to_i((val).f)); \
        break;                                                                                                                   \
      default:                                                                                                                   \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))0;                                                                                  \
        break;                                                                                                                   \
    }                                                                                                                            \
  } while (0)

float vm_read_as_f32(vm_obj_t_e type, const void* src);
int32_t vm_read_as_i32(vm_obj_t_e type, const void* src);
int64_t vm_read_as_i64(vm_obj_t_e type, const void* src);

#define VM_LOAD_CAST_TO(dst_ptr, type, src)                                                                                                     \
  do {                                                                                                                                          \
    const void* __lc_s = (const void*)(src);                                                                                                    \
    vm_obj_t_e __lc_t = (type);                                                                                                                 \
    *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr),                                                                                   \
        float: (likely(__lc_t == VM_OBJ_F) ? *(const float*)__lc_s : vm_read_as_f32(__lc_t, __lc_s)),                                           \
        double: (likely(__lc_t == VM_OBJ_F) ? (double)*(const float*)__lc_s : (double)vm_read_as_f32(__lc_t, __lc_s)),                          \
        int64_t: (likely(__lc_t == VM_OBJ_U64) ? *(const int64_t*)__lc_s : vm_read_as_i64(__lc_t, __lc_s)),                                     \
        uint64_t: (likely(__lc_t == VM_OBJ_U64) ? *(const uint64_t*)__lc_s : (uint64_t)vm_read_as_i64(__lc_t, __lc_s)),                         \
        int32_t: (likely(__lc_t == VM_OBJ_I32 || __lc_t == VM_OBJ_U32) ? *(const int32_t*)__lc_s : vm_read_as_i32(__lc_t, __lc_s)),             \
        uint32_t: (likely(__lc_t == VM_OBJ_U32 || __lc_t == VM_OBJ_I32) ? *(const uint32_t*)__lc_s : (uint32_t)vm_read_as_i32(__lc_t, __lc_s)), \
        default: vm_read_as_i32(__lc_t, __lc_s));                                                                                               \
  } while (0)

#define VM_LOAD_CAST_TO_FAST(dst_ptr, type, src)                                                                              \
  do {                                                                                                                        \
    const void* __lf_s = (const void*)(src);                                                                                  \
    switch (type) {                                                                                                           \
      case VM_OBJ_U8:                                                                                                         \
      case VM_OBJ_B:                                                                                                          \
      case VM_OBJ_STR:                                                                                                        \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))*(const uint8_t*)__lf_s;                                                         \
        break;                                                                                                                \
      case VM_OBJ_U32:                                                                                                        \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))*(const uint32_t*)__lf_s;                                                        \
        break;                                                                                                                \
      case VM_OBJ_I32:                                                                                                        \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))*(const int32_t*)__lf_s;                                                         \
        break;                                                                                                                \
      case VM_OBJ_U64: {                                                                                                      \
        uint64_t __lf_v;                                                                                                      \
        memcpy(&__lf_v, __lf_s, sizeof(__lf_v));                                                                              \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))__lf_v;                                                                          \
        break;                                                                                                                \
      }                                                                                                                       \
      case VM_OBJ_F: {                                                                                                        \
        float __lf_f = *(const float*)__lf_s;                                                                                 \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr), float: __lf_f, double: __lf_f, default: vm_f_to_i(__lf_f)); \
        break;                                                                                                                \
      }                                                                                                                       \
      default:                                                                                                                \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))0;                                                                               \
        break;                                                                                                                \
    }                                                                                                                         \
  } while (0)

static __always_inline err_h vm_store_inline(vm_obj_h owner, vm_payload_t slot, vm_val_t v, vm_obj_t_e src_type, uint16_t err_id) {
  if (likely(slot.type == src_type)) {
    switch (src_type) {
      case VM_OBJ_F:
        *(float*)slot.ptr = v.f;
        break;
      case VM_OBJ_U8:
      case VM_OBJ_B:
      case VM_OBJ_STR:
        *(uint8_t*)slot.ptr = v.u8;
        break;
      case VM_OBJ_U32:
      case VM_OBJ_I32:
        *(uint32_t*)slot.ptr = v.u32;
        break;
      case VM_OBJ_U64:
        memcpy(slot.ptr, &v.u64, sizeof(v.u64));
        break;
      default:
        return vm_obj_not_scalar_err(owner, slot.type, err_id);
    }
    owner->head.f.upd = 1;
    return NULL;
  }

  switch (slot.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      VM_VAL_CAST_TO((uint8_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_U32:
      VM_VAL_CAST_TO((uint32_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_I32:
      VM_VAL_CAST_TO((int32_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_U64: {
      uint64_t tmp;
      VM_VAL_CAST_TO(&tmp, v, src_type);
      memcpy(slot.ptr, &tmp, sizeof(tmp));
      break;
    }
    case VM_OBJ_F:
      VM_VAL_CAST_TO((float*)slot.ptr, v, src_type);
      break;
    default:
      return vm_obj_not_scalar_err(owner, slot.type, err_id);
  }
  owner->head.f.upd = 1;
  return NULL;
}

static __always_inline err_h vm_obj_set_scalar_direct(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type) {
  if (unlikely(obj == NULL)) return vm_obj_null_obj_err();
  if (unlikely(!obj->head.f.mutable)) return vm_obj_not_mutable_err(obj);
  uint8_t* p = vm_obj_elem_ptr(obj, index);
  if (unlikely(p == NULL)) return vm_obj_oob_err(obj, index);
  // VM_ID_NONE, not 0: this path was handed a handle, and 0 is a real accessor id
  return vm_store_inline(obj, (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0}, v, src_type, VM_ID_NONE);
}

#define VM_TYPE_OF(x) _Generic((x), uint8_t: VM_OBJ_U8, int8_t: VM_OBJ_I32, char: VM_OBJ_U8, uint16_t: VM_OBJ_U32, int16_t: VM_OBJ_I32, uint32_t: VM_OBJ_U32, int32_t: VM_OBJ_I32, uint64_t: VM_OBJ_U64, int64_t: VM_OBJ_U64, float: VM_OBJ_F, double: VM_OBJ_F, bool: VM_OBJ_B, default: VM_OBJ_NONE)

#define VM_VAL_OF(x)                              \
  _Generic((x),                                   \
      float: (vm_val_t){.f = (float)(x)},         \
      double: (vm_val_t){.f = (float)(x)},        \
      bool: (vm_val_t){.u8 = (uint8_t)!!(x)},     \
      uint8_t: (vm_val_t){.u8 = (uint8_t)(x)},    \
      char: (vm_val_t){.u8 = (uint8_t)(x)},       \
      int8_t: (vm_val_t){.i32 = (int32_t)(x)},    \
      int16_t: (vm_val_t){.i32 = (int32_t)(x)},   \
      int32_t: (vm_val_t){.i32 = (int32_t)(x)},   \
      uint64_t: (vm_val_t){.u64 = (uint64_t)(x)}, \
      int64_t: (vm_val_t){.u64 = (uint64_t)(x)},  \
      default: (vm_val_t){.u32 = (uint32_t)(x)})

/** @brief Read one converted scalar out of `source` into the local `output`. */
#define VM_OBJ_GET_VAL(output, source)                     \
  ({                                                       \
    const vm_accessor_t* __gv_a = (source);                \
    vm_resolved_t __gv_r;                                  \
    err_h __gv_e = NULL;                                   \
    const void* __gv_ptr;                                  \
    vm_obj_t_e __gv_t;                                     \
    if (likely(vm_resolve_fast(__gv_a, false, &__gv_r))) { \
      __gv_ptr = __gv_r.payload.ptr;                       \
      __gv_t = (vm_obj_t_e)__gv_r.payload.type;            \
    } else {                                               \
      vm_payload_t __gv_p;                                 \
      __gv_e = vm_obj_get_payload(&__gv_p, __gv_a);        \
      __gv_ptr = __gv_p.ptr;                               \
      __gv_t = (vm_obj_t_e)__gv_p.type;                    \
    }                                                      \
    if (likely(!__gv_e)) {                                 \
      VM_LOAD_CAST_TO(&(output), __gv_t, __gv_ptr);        \
    }                                                      \
    __gv_e;                                                \
  })

/** @brief Write scalar `source` into `target`, converting to stored type. */
#define VM_OBJ_SET_VAL(source, target)                                                                                                                                      \
  ({                                                                                                                                                                        \
    const vm_accessor_t* __sv_a = (target);                                                                                                                                 \
    vm_val_t __sv_v = VM_VAL_OF(source);                                                                                                                                    \
    vm_obj_t_e __sv_t = VM_TYPE_OF(source);                                                                                                                                 \
    vm_resolved_t __sv_r;                                                                                                                                                   \
    likely(vm_resolve_fast(__sv_a, true, &__sv_r)) ? vm_store_inline(__sv_r.owner, __sv_r.payload, __sv_v, __sv_t, __sv_a->id) : vm_obj_set_scalar(__sv_a, __sv_v, __sv_t); \
  })

/** @brief Write scalar `source` straight into an owned output object. */
#define VM_OBJ_SET_VAL_AT(source, obj, index) vm_obj_set_scalar_direct((obj), (index), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief Read one converted scalar out of an already-resolved payload. */
#define VM_PAYLOAD_GET_VAL(output, payload)                     \
  do {                                                          \
    vm_payload_t __pv_p = (payload);                            \
    if (likely(__pv_p.ptr != NULL)) {                           \
      VM_LOAD_CAST_TO_FAST(&(output), __pv_p.type, __pv_p.ptr); \
    } else {                                                    \
      (output) = (__typeof__(output))0;                         \
    }                                                           \
  } while (0)
