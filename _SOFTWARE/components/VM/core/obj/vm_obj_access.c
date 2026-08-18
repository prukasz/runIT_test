#include "vm_obj_access.h"
#include <string.h>
#include "esp_compiler.h"

#define OWNER OWNER_VM_ACCESSOR

/* Everything vm_resolve_fast() misses funnels through resolve_d(), which walks
   an accessor's index chain and returns both the payload and its owner object
   -- only the walk itself has both in hand at once. */

// one element of obj as a payload, bounds-checked (see vm_obj_elem_ptr())
static __always_inline vm_payload_t obj_elem(vm_obj_h obj, uint32_t i) {
  uint8_t* p = vm_obj_elem_ptr(obj, i);
  if (unlikely(!p)) return (vm_payload_t){.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  return (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
}

// ---------------------------------------------------------------------------
// Read conversion -- one copy of each, shared by every pin. `src` is assumed
// non-NULL and to point at an element of `type`. See VM_LOAD_CAST_TO in the
// header for why these stay out of line.
// ---------------------------------------------------------------------------

float vm_read_as_f32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (float)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (float)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return (float)*(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));  // payload is 4-aligned, this read is 8 wide
      return (float)v;
    }
    case VM_OBJ_F:
      return *(const float*)src;
    default:  // PTR/NONE aren't scalars
      return 0.0f;
  }
}

int32_t vm_read_as_i32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (int32_t)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (int32_t)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return *(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));
      return (int32_t)v;
    }
    case VM_OBJ_F:
      // rounds and saturates -- a plain cast would be UB, and this is
      // reachable from ordinary user wiring, not just corruption
      return (int32_t)vm_f_to_i(*(const float*)src);
    default:
      return 0;
  }
}

int64_t vm_read_as_i64(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (int64_t)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (int64_t)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return (int64_t)*(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));
      return (int64_t)v;
    }
    case VM_OBJ_F:
      return vm_f_to_i(*(const float*)src);
    default:
      return 0;
  }
}

// Kept noinline: a float index is rare, but inlining it drags roundf and
// __fixsfdi into resolve_d for every caller. Measured: taking it out of line
// cost a by-ref access two calls (405 -> 418 cyc) and shrank everything else.
static __attribute__((noinline)) uint32_t index_from_float(const void* src) {
  return (uint32_t)vm_f_to_i(*(const float*)src);
}

static __always_inline uint32_t payload_as_index(vm_payload_t p) {
  if (unlikely(!p.ptr)) return 0;
  switch (p.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return *(const uint8_t*)p.ptr;
    case VM_OBJ_U32:
    case VM_OBJ_I32:
      return *(const uint32_t*)p.ptr;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, p.ptr, sizeof(v));
      return (uint32_t)v;
    }
    case VM_OBJ_F:
      return index_from_float(p.ptr);
    default:
      return 0;
  }
}

// Index of `parent`'s child (a VM_OBJ_PTR array) tagged `name`, or -1. Linear
// with a compare per child -- see VM_IDX_NAME in the header.
static __always_inline int32_t find_child_by_name(vm_obj_h parent, const char* name, uint8_t n) {
  if (unlikely(n == 0 || n > VM_OBJ_NAME_MAX)) return -1;
  char first_char = name[0];

  uint16_t cnt = vm_obj_items_cnt(parent);
  vm_obj_h* children = (vm_obj_h*)parent->payload;
  for (uint16_t i = 0; i < cnt; i++) {
    vm_obj_h c = children[i];
    if (unlikely(!c)) continue;  // unwired slot
    uint8_t tag_len = 0;
    const char* tag = vm_obj_tag(c, &tag_len);
    if (tag == NULL || tag_len != n || tag[0] != first_char) continue;

    // open-coded rather than memcmp(): n isn't a compile-time constant, so
    // GCC emits a real call, and every same-prefix child (temp/time, val1/
    // val2) was paying call8+entry/retw to compare one byte
    uint8_t k = 1;
    while (k < n && tag[k] == name[k]) k++;
    if (k == n) return (int32_t)i;
  }
  return -1;
}

vm_obj_h vm_obj_find_child(vm_obj_h parent, const char* tag) {
  if (unlikely(!parent || !tag || (vm_obj_t_e)parent->head.d.obj_t != VM_OBJ_PTR)) return NULL;
  size_t len = strlen(tag);
  if (unlikely(len == 0 || len > VM_OBJ_NAME_MAX)) return NULL;
  int32_t idx = find_child_by_name(parent, tag, (uint8_t)len);
  if (idx < 0) return NULL;
  return ((vm_obj_h*)parent->payload)[idx];
}

// ---------------------------------------------------------------------------
// resolve_d's error arms, kept out of its body: eight inlined SE_ERR_NEW
// sites gave the walk a 160-byte stack frame -- multiplied by
// VM_ACCESSOR_MAX_DEPTH on a by-ref chain -- none of which runs on a working
// program. SE_ERR_NEW records no file/line, so moving them here costs the
// trace nothing (tag + OWNER + payload already identify the site).
// ---------------------------------------------------------------------------

static __attribute__((noinline)) err_h err_depth(uint16_t id) {
  SE_RET_ERR(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, .id = id);
}

static __attribute__((noinline)) err_h err_unknown_id(uint16_t id) {
  SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
}

// serves both the by-name parent check and the mid-chain step: both expect PTR
static __attribute__((noinline)) err_h err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, vm_obj_h obj) {
  SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = id, .chain_pos = pos, .expected = VM_OBJ_PTR, .actual = actual, .obj = (void*)obj);
}

static __attribute__((noinline)) err_h err_oob(uint16_t id, uint8_t pos, uint32_t index, vm_obj_h obj) {
  // saturate the 16-bit payload field so a huge index still reads as
  // "past the end" rather than a wrapped small number
  SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = id, .chain_pos = pos, .index = (uint16_t)(index > UINT16_MAX ? UINT16_MAX : index), .obj = (void*)obj);
}

static __attribute__((noinline)) err_h err_null_obj(uint16_t id, uint8_t pos, vm_obj_h parent) {
  SE_RET_ERR(ERR_VM_ACCESSOR_NULL_OBJ, .id = id, .chain_pos = pos, .parent_obj = (void*)parent);
}

static __attribute__((noinline)) err_h err_not_mutable(uint16_t id, uint8_t pos, vm_obj_h obj) {
  SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = id, .chain_pos = pos, .obj = (void*)obj);
}

static __attribute__((noinline)) err_h err_index_failed(err_h cause, uint16_t id, uint8_t pos) {
  return SE_WRAP_ERR(cause, ERR_VM_ACCESSOR_INDEX_FAILED, .id = id, .chain_pos = pos);
}

// built by hand, not SE_RET_ERR: the payload carries a copied string, and
// designated initialisers can't fill a char array
static __attribute__((noinline)) err_h name_not_found_err(uint16_t id, uint8_t chain_pos, const char* name) {
  err_h e = SE_ERR_NEW(ERR_VM_ACCESSOR_NAME_NOT_FOUND, .id = id, .chain_pos = chain_pos);
  err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t* pl = (err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t*)e->payload;
  size_t n = strlen(name);
  if (n >= sizeof(pl->name)) n = sizeof(pl->name) - 1;
  memcpy(pl->name, name, n);
  pl->name[n] = '\0';
  return e;
}

// `for_write` gates the mutability check (reads never need it); lives here,
// not in the SET entry points, because this is the last place still holding
// the owning object's header.
static err_h resolve_d(const vm_accessor_t* acc, uint8_t depth, bool for_write, vm_resolved_t* out) {
  out->payload = (vm_payload_t){.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  out->owner = NULL;

  if (unlikely(depth >= VM_ACCESSOR_MAX_DEPTH)) return err_depth(acc->id);

  vm_obj_h obj = vm_obj_by_id(acc->id);
  if (unlikely(!obj)) return err_unknown_id(acc->id);

  vm_payload_t p = vm_obj_as_payload(obj);
  for (uint8_t i = 0; i < acc->count; i++) {
    const vm_index_t* idx = &acc->indices[i];
    uint32_t index;
    switch (idx->kind) {
      case VM_IDX_REF: {
        // Try the non-recursive fast path first: it's the common case
        // (object(9), a step-table element) and the recursive call is the
        // expensive part of a by-ref chain, not the read. The depth check
        // comes first because the fast path resolves a level without a
        // frame -- letting it take the last level would let MAX_DEPTH+1
        // fit in MAX_DEPTH frames.
        vm_resolved_t sub;
        if (likely(depth + 1 < VM_ACCESSOR_MAX_DEPTH && vm_resolve_fast(idx->ref, false, &sub))) {
          index = payload_as_index(sub.payload);
          break;
        }
        err_h e = resolve_d(idx->ref, depth + 1, false, &sub);
        if (unlikely(e)) return err_index_failed(e, acc->id, i);
        index = payload_as_index(sub.payload);
        break;
      }
      case VM_IDX_NAME: {
        // checked against `obj`, not `p`: past the first step `p` still
        // describes the previous element, while `obj` is what's about to
        // be indexed, and only a PTR array has tagged children
        if (unlikely((vm_obj_t_e)obj->head.d.obj_t != VM_OBJ_PTR)) {
          return err_expected_ptr(acc->id, i, obj->head.d.obj_t, obj);
        }
        int32_t found = find_child_by_name(obj, idx->name, idx->name_len);
        if (unlikely(found < 0)) {
          return name_not_found_err(acc->id, i, idx->name);  // routine for variable-shape messages, not necessarily a bug
        }
        index = (uint32_t)found;
        break;
      }
      case VM_IDX_LITERAL:
      default:
        index = idx->value;
        break;
    }

    p = obj_elem(obj, index);
    if (unlikely(!p.ptr)) return err_oob(acc->id, i, index, obj);

    if (i + 1 < acc->count) {
      if (unlikely(p.type != VM_OBJ_PTR)) return err_expected_ptr(acc->id, i, (uint8_t)p.type, obj);
      vm_obj_h child = *(vm_obj_h*)p.ptr;
      if (unlikely(!child)) {
        return err_null_obj(acc->id, i, obj);  // untrusted-input counterpart to TYPE_MISMATCH: a packet-sourced tree isn't guaranteed fully wired
      }
      obj = child;
    }
  }

  if (unlikely(for_write && !obj->head.f.mutable)) return err_not_mutable(acc->id, acc->count, obj);

  out->payload = p;
  out->owner = obj;
  return NULL;
}

// cold half of vm_store_inline() (see header): built here, where there's an
// ambient OWNER, since it never runs on a working program
err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id) {
  SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = id, .chain_pos = 0, .expected = VM_OBJ_NONE, .actual = actual, .obj = (void*)owner);
}

err_h vm_obj_get_payload(vm_payload_t* target, const vm_accessor_t* source) {
  vm_resolved_t r;
  if (likely(vm_resolve_fast(source, false, &r))) {
    *target = r.payload;
    return NULL;
  }
  SE_RET_IF_ERR(resolve_d(source, 0, false, &r));
  *target = r.payload;
  return NULL;
}

err_h vm_get_obj(vm_obj_h* target, const vm_accessor_t* source) {
  vm_resolved_t r;
  if (unlikely(!vm_resolve_fast(source, false, &r))) {
    SE_RET_IF_ERR(resolve_d(source, 0, false, &r));
  }

  // A chain landing on a pointer element means "the object behind this
  // slot" (the point of a switch/demux cell), so follow it. `count > 0`
  // matters: a chainless accessor names the object itself, so a PTR
  // container must come back as-is rather than silently substituting child[0].
  if (source->count > 0 && r.payload.type == VM_OBJ_PTR && r.payload.ptr && r.payload.count >= 1) {
    vm_obj_h linked = *(vm_obj_h*)r.payload.ptr;
    if (!linked) {
      SE_RET_ERR(ERR_VM_ACCESSOR_NULL_OBJ, .id = source->id, .chain_pos = source->count, .parent_obj = (void*)r.owner);
    }
    *target = linked;
    return NULL;
  }

  *target = r.owner;
  return NULL;
}

err_h vm_obj_set_scalar(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type) {
  vm_resolved_t r;
  if (likely(vm_resolve_fast(target, true, &r))) {
    return vm_store_inline(r.owner, r.payload, v, src_type, target->id);
  }
  SE_RET_IF_ERR(resolve_d(target, 0, true, &r));
  return vm_store_inline(r.owner, r.payload, v, src_type, target->id);
}

void vm_obj_clear_quiet(vm_obj_h obj) {
  if (!obj || (vm_obj_t_e)obj->head.d.obj_t == VM_OBJ_PTR) return;
  if (obj->head.payload_size) memset(obj->payload, 0, obj->head.payload_size);
}

// cold arms of the direct write path -- see vm_obj_set_scalar_direct() in the header
err_h vm_obj_null_obj_err(void) {
  SE_RET_ERR(ERR_NULL_PTR, 0);
}

err_h vm_obj_not_mutable_err(vm_obj_h obj) {
  SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = 0, .chain_pos = 0, .obj = (void*)obj);
}

err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index) {
  SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = 0, .chain_pos = 0, .index = index, .obj = (void*)obj);
}

err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child) {
  SE_CHECK_NOT_NULL(cell);
  SE_CHECK_NOT_NULL(child);
  if (!cell->head.f.mutable) {
    SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = 0, .chain_pos = 0, .obj = (void*)cell);
  }
  vm_payload_t slot = obj_elem(cell, index);
  if (!slot.ptr) {
    SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = 0, .chain_pos = 0, .index = index, .obj = (void*)cell);
  }
  if (slot.type != VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = 0, .chain_pos = 0, .expected = VM_OBJ_PTR, .actual = slot.type, .obj = (void*)cell);
  }
  *(vm_obj_h*)slot.ptr = child;
  cell->head.f.upd = 1;
  return NULL;
}

err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target) {
  vm_resolved_t src, dst;
  SE_RET_IF_ERR(resolve_d(source, 0, false, &src));
  SE_RET_IF_ERR(resolve_d(target, 0, true, &dst));

  // PTR payloads are addresses into this program's arena -- copying the
  // bytes would alias two graphs onto one child; use vm_obj_link() instead.
  if (src.payload.type == VM_OBJ_PTR || dst.payload.type == VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = src.payload.type, .dst_type = dst.payload.type, .src_size = src.payload.count, .dst_size = dst.payload.count);
  }

  uint8_t w = vm_type_width(src.payload.type);
  if (src.payload.type != dst.payload.type || src.payload.count != dst.payload.count || w == 0) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = src.payload.type, .dst_type = dst.payload.type, .src_size = src.payload.count, .dst_size = dst.payload.count);
  }

  memcpy(dst.payload.ptr, src.payload.ptr, (size_t)w * src.payload.count);
  dst.owner->head.f.upd = 1;
  return NULL;
}

err_h vm_obj_link(const vm_accessor_t* to_join, const vm_accessor_t* owner) {
  vm_obj_h child;
  SE_RET_IF_ERR(vm_get_obj(&child, to_join));

  vm_resolved_t slot;
  SE_RET_IF_ERR(resolve_d(owner, 0, true, &slot));

  if (slot.payload.type != VM_OBJ_PTR || !slot.payload.ptr) {
    SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = owner->id, .chain_pos = owner->count, .expected = VM_OBJ_PTR, .actual = slot.payload.type, .obj = (void*)slot.owner);
  }

  *(vm_obj_h*)slot.payload.ptr = child;
  slot.owner->head.f.upd = 1;
  return NULL;
}
