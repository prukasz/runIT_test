#include "vm_obj_access.h"

#include <string.h>

#include "vm_obj_dyn.h"

#define OWNER OWNER_VM_ACCESSOR

/*
 * Object Access & Resolution Engine
 *
 * Logic Flow:
 *   1. Internal Helpers:
 *      - Permission validation (writable)
 *      - Element indexing & tag matching (obj_elem, find_child_by_name)
 *      - Read & index conversion (vm_get_as_*, payload_as_index)
 *      - Accessor resolution walk (resolve_d, resolve_acc)
 *      - Store conversion & dynamic slot management (vm_store_converted, slot_store)
 *      - Tree comparison, copy, and clone engines (copy_values, copy_tree, shape_matches, clone_shape, clone_into)
 *   2. Target Public APIs:
 *      - Read APIs (vm_obj_get_payload, vm_obj_get_obj, vm_obj_get_owner, vm_obj_get_child)
 *      - Write APIs (vm_obj_set_scalar, vm_obj_clear_quiet)
 *      - Copy & Clone APIs (vm_obj_copy_content, vm_obj_shape_matches, vm_obj_schema_matches, vm_obj_clone_shape, vm_obj_clone_into)
 *      - Link APIs (vm_obj_link, vm_obj_link_direct)
 *      - Publication APIs (vm_obj_publish)
 *      - User Mutation Boundary APIs (_usr variants)
 */

// ===========================================================================
// 1. Internal Helpers
// ===========================================================================

// --- Permission Validation ---

static err_h writable(vm_obj_h obj, bool user) {
  if (!obj) return vm_obj_null_obj_err();
  if (!obj->head.f.mutable) return vm_obj_not_mutable_err(obj);
  if (user && obj->head.f.usr_protected) {
    SE_RET_ERR(ERR_VM_OBJ_USR_PROTECTED, .obj = (void*)obj);
  }
  return NULL;
}

// --- Element Indexing & Tag Matching ---

// one element of obj as a payload, bounds-checked (see vm_obj_get_elem_ptr())
static __always_inline vm_payload_t obj_elem(vm_obj_h obj, uint32_t i) {
  uint8_t* p = vm_obj_get_elem_ptr(obj, i);
  if (unlikely(!p)) return (vm_payload_t){.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  return (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
}

// Index of `parent`'s child (a VM_OBJ_PTR array) tagged `name`, or -1. Linear
// with a compare per child -- see VM_IDX_NAME in the header.
static __always_inline int32_t find_child_by_name(vm_obj_h parent, const char* name, uint8_t n) {
  if (unlikely(n == 0 || n > VM_OBJ_NAME_MAX)) return -1;
  char first_char = name[0];

  uint16_t cnt = vm_obj_get_items_cnt(parent);
  vm_obj_h* children = (vm_obj_h*)parent->payload;
  for (uint16_t i = 0; i < cnt; i++) {
    vm_obj_h c = children[i];
    if (unlikely(!c)) continue;  // unwired slot
    uint8_t tag_len = 0;
    const char* tag = vm_obj_get_tag(c, &tag_len);
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

// --- Read & Index Conversions ---

float vm_get_as_f32(vm_obj_t_e type, const void* src) {
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

int64_t vm_get_as_i64(vm_obj_t_e type, const void* src) {
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
      return vm_internal_f_to_i(*(const float*)src);
    default:
      return 0;
  }
}

int32_t vm_get_as_i32(vm_obj_t_e type, const void* src) {
  return (int32_t)vm_get_as_i64(type, src);
}

static __always_inline uint32_t payload_as_index(vm_payload_t p) {
  if (unlikely(!p.ptr)) return 0;
  return (uint32_t)vm_get_as_i64((vm_obj_t_e)p.type, p.ptr);
}

// --- Accessor Resolution Walk ---

/* Everything vm_resolve_fast() misses funnels through resolve_d(), which walks
   an accessor's index chain and returns both the payload and its owner object
   -- only the walk itself has both in hand at once. */

// `for_write` gates the mutability check (reads never need it); lives here,
// not in the SET entry points, because this is the last place still holding
// the owning object's header.
static err_h resolve_d(const vm_accessor_t* acc, uint8_t depth, bool for_write, vm_resolved_t* out) {
  out->payload = (vm_payload_t){.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  out->owner = NULL;

  if (unlikely(depth >= VM_ACCESSOR_MAX_DEPTH)) return vm_err_depth(acc->id);

  vm_obj_h obj = vm_obj_get_by_id(acc->id);
  if (unlikely(!obj)) return vm_err_unknown_id(acc->id);

  vm_payload_t p = vm_make_payload(obj);
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
        if (unlikely(e)) return vm_err_index_failed(e, acc->id, i);
        index = payload_as_index(sub.payload);
        break;
      }
      case VM_IDX_NAME: {
        // checked against `obj`, not `p`: past the first step `p` still
        // describes the previous element, while `obj` is what's about to
        // be indexed, and only a PTR array has tagged children
        if (unlikely((vm_obj_t_e)obj->head.d.obj_t != VM_OBJ_PTR)) {
          return vm_err_expected_ptr(acc->id, i, obj->head.d.obj_t, obj);
        }
        int32_t found = find_child_by_name(obj, idx->name, idx->name_len);
        if (unlikely(found < 0)) {
          return vm_err_name_not_found(acc->id, i, idx->name);  // routine for variable-shape messages, not necessarily a bug
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
    if (unlikely(!p.ptr)) return vm_err_chain_oob(acc->id, i, index, obj);

    if (i + 1 < acc->count) {
      if (unlikely(p.type != VM_OBJ_PTR)) return vm_err_expected_ptr(acc->id, i, (uint8_t)p.type, obj);
      vm_obj_h child = *(vm_obj_h*)p.ptr;
      if (unlikely(!child)) {
        return vm_err_null_obj(acc->id, i, obj);  // untrusted-input counterpart to TYPE_MISMATCH: a packet-sourced tree isn't guaranteed fully wired
      }
      obj = child;
    }
  }

  if (unlikely(for_write && !obj->head.f.mutable)) return vm_err_chain_not_mutable(acc->id, acc->count, obj);

  out->payload = p;
  out->owner = obj;
  return NULL;
}

static __always_inline err_h resolve_acc(const vm_accessor_t* acc, bool for_write, vm_resolved_t* out) {
  if (likely(vm_resolve_fast(acc, for_write, out))) return NULL;
  return resolve_d(acc, 0, for_write, out);
}

// --- Store & Dynamic Slot Management ---

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
        *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr), float: (val).f, double: (val).f, default: vm_internal_f_to_i((val).f)); \
        break;                                                                                                                   \
      default:                                                                                                                   \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))0;                                                                                  \
        break;                                                                                                                   \
    }                                                                                                                            \
  } while (0)

err_h vm_internal_store_converted(vm_obj_h owner, vm_payload_t slot, vm_val_t v, vm_obj_t_e src_type, uint16_t err_id) {
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

/*
A pointer slot is the only place an object is *owned*, so it is the only place
a reference count moves -- which is why both link entry points write through
here rather than storing the handle themselves.

The new child is retained before the old one is released. A program re-linking
a cell to something that lives inside the tree it is replacing would otherwise
free the subtree the new handle points into, one statement before installing
it. Both calls fall straight out on an arena object (one bit -- see
vm_obj_dyn_get_id), so a program's whole load-time wiring pays nothing for this.
*/
static err_h slot_store(vm_obj_h owner, vm_obj_h* cell, vm_obj_h child) {
  vm_obj_h prev = *cell;
  if (prev == child) return NULL;  // already there: no churn, and no news to publish
  SE_RET_IF_ERR(vm_obj_dyn_check_link(owner, cell, child));

  vm_obj_dyn_retain(child);
  *cell = child;
  vm_obj_dyn_release(prev);

  owner->head.f.upd = 1;
  return NULL;
}

// --- Tree Copy, Clone & Comparison Engine ---

/* One payload of plain values. `s.ptr == d.ptr` is a copy onto itself, which
   is a no-op rather than an error -- and skipping it also keeps memcpy() off
   a source and destination that are the same bytes. */
static err_h copy_values(vm_payload_t s, vm_payload_t d, vm_obj_h d_owner, bool commit) {
  uint8_t w = vm_type_width((vm_obj_t_e)s.type);
  if (unlikely(s.type != d.type || s.count != d.count || w == 0)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = s.type, .dst_type = d.type, .src_size = s.count, .dst_size = d.count);
  }
  if (commit) {
    if (likely(s.ptr != d.ptr)) memmove(d.ptr, s.ptr, (size_t)w * s.count);
    d_owner->head.f.upd = 1;
  }
  return NULL;
}

/*
A pointer array is copied by walking it, never by copying its bytes. Those
bytes are addresses into this program's arena, so duplicating them would leave
both trees sharing one set of children -- and an edit made through the copy
would then show up in the original, at some unrelated point in the program,
which is the kind of bug nobody traces back to a Copy block. Nothing below
ever writes a pointer, so that cannot happen: the two trees keep their own
children and only values move between them.

The destination supplies the shape. Its children must already exist and match
the source's, which is what lets a deep copy allocate nothing -- it is a walk
over two structures that are already there, not a clone. A destination shaped
differently from its source is a wiring error and says so.
*/
static err_h copy_tree(vm_payload_t s, vm_payload_t d, vm_obj_h d_owner, uint8_t depth, bool commit, bool user) {
  SE_RET_IF_ERR(writable(d_owner, user));
  if (likely(s.type != VM_OBJ_PTR && d.type != VM_OBJ_PTR)) return copy_values(s, d, d_owner, commit);

  if (unlikely(s.type != d.type || s.count != d.count)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = s.type, .dst_type = d.type, .src_size = s.count, .dst_size = d.count);
  }
  if (unlikely(depth >= VM_OBJ_COPY_MAX_DEPTH)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = 0, .depth = depth, .reason = VM_COPY_SHAPE_DEPTH);
  }

  vm_obj_h* sc = (vm_obj_h*)s.ptr;
  vm_obj_h* dc = (vm_obj_h*)d.ptr;
  for (uint16_t i = 0; i < s.count; i++) {
    if (sc[i] == dc[i]) continue;  // both unwired, or literally the same child
    if (unlikely(!sc[i] || !dc[i])) {
      SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = i, .depth = depth, .reason = sc[i] ? VM_COPY_SHAPE_DST_EMPTY : VM_COPY_SHAPE_SRC_EMPTY);
    }
    // checked per child: the walk writes into each of them, and mutability is
    // a property of the object, not of the accessor that reached its root
    SE_RET_IF_ERR(copy_tree(vm_make_payload(sc[i]), vm_make_payload(dc[i]), dc[i], (uint8_t)(depth + 1), commit, user));
  }

  /* The parent's own bytes did not change, but what hangs under it did, and a
     block whose accessor names the whole table has no other place to see that. */
  if (commit) d_owner->head.f.upd = 1;
  return NULL;
}

/* Same walk as copy_tree, asking only whether the two structures agree.
   payload_size carries the element count for a known type, so one compare
   covers both. Unwired slots must line up too: a source child with no
   destination child to receive it is a different shape, not a copy. */
static bool shape_matches(vm_obj_h a, vm_obj_h b, uint8_t depth, bool schema) {
  if (!a || !b) return a == b;
  if (a->head.d.obj_t != b->head.d.obj_t || a->head.payload_size != b->head.payload_size) return false;
  if (schema) {
    if (a->head.f.tagged != b->head.f.tagged || a->head.d.name_size != b->head.d.name_size) return false;
    if (a->head.d.name_size && memcmp(a->payload + a->head.payload_size,
                                     b->payload + b->head.payload_size, a->head.d.name_size)) return false;
  }
  if ((vm_obj_t_e)a->head.d.obj_t != VM_OBJ_PTR) return true;
  if (unlikely(depth >= VM_OBJ_COPY_MAX_DEPTH)) return false;

  vm_obj_h* ka = (vm_obj_h*)a->payload;
  vm_obj_h* kb = (vm_obj_h*)b->payload;
  uint16_t n = vm_obj_get_items_cnt(a);
  for (uint16_t i = 0; i < n; i++) {
    if (!shape_matches(ka[i], kb[i], (uint8_t)(depth + 1), schema)) return false;
  }
  return true;
}

static err_h copy_content(const vm_accessor_t* source, const vm_accessor_t* target, bool user) {
  vm_resolved_t src, dst;
  SE_RET_IF_ERR(resolve_acc(source, false, &src));
  SE_RET_IF_ERR(resolve_acc(target, true, &dst));
  SE_RET_IF_ERR(copy_tree(src.payload, dst.payload, dst.owner, 0, false, user));
  return copy_tree(src.payload, dst.payload, dst.owner, 0, true, user);
}

/*
Build a tree shaped like `src` with its values left zero.

Every object it makes is dynamic, because the arena cannot free and this one
has to be replaceable -- a Clone whose source changes shape drops the previous
tree, and a bump allocator has no way to take it back. That is the whole reason
vm_obj_dyn exists, and this is its first caller outside the self test.

Three flags are the clone's own rather than the source's. `mutable`, because a
destination that cannot be written is useless. `retentive` cleared, because
this lives on the heap and is gone at the next load, so NVS has no business
with it. `upd_resetable` set, so the sweep withdraws its freshness at the end
of the pass that filled it -- a snapshot is news once, not forever.
*/
static err_h clone_shape(vm_obj_h* out, vm_obj_h src, uint8_t depth) {
  *out = NULL;
  if (unlikely(src->head.d.obj_t == VM_OBJ_PTR && depth >= VM_OBJ_COPY_MAX_DEPTH)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = 0, .depth = depth, .reason = VM_COPY_SHAPE_DEPTH);
  }

  vm_obj_head_t h = src->head;
  h.f.mutable = 1;
  h.f.usr_protected = 0;  // the snapshot belongs to its caller, not the source producer
  h.f.retentive = 0;
  h.f.upd_resetable = 1;

  uint8_t name_len = 0;
  const char* name = vm_obj_get_tag(src, &name_len);

  vm_obj_h o = NULL;
  SE_RET_IF_ERR(vm_obj_dyn_create(&o, &h, name));

  if ((vm_obj_t_e)h.d.obj_t == VM_OBJ_PTR) {
    vm_obj_h* sk = (vm_obj_h*)src->payload;
    vm_obj_h* dk = (vm_obj_h*)o->payload;
    uint16_t n = vm_obj_get_items_cnt(src);
    for (uint16_t i = 0; i < n; i++) {
      if (!sk[i]) continue;  // unwired in the source, unwired in the copy
      vm_obj_h kid = NULL;
      err_h e = clone_shape(&kid, sk[i], (uint8_t)(depth + 1));
      if (unlikely(e)) {
        /* Half-built, and owned by nothing: releasing the root takes every
           child with it, so a rejected clone leaks neither a slot nor a byte
           -- the same rule vm_obj_create() follows for a rejected shape. */
        vm_obj_dyn_release(o);
        return e;
      }
      dk[i] = kid;
      vm_obj_dyn_retain(kid);
    }
  }

  *out = o;
  return NULL;
}

static err_h clone_into(const vm_accessor_t* source, const vm_accessor_t* target, bool user) {
  /* vm_obj_get_obj(), not a payload resolve: a Clone wants the object behind the
     source, header and all, because the header is the shape it has to match. */
  vm_obj_h src = NULL;
  SE_RET_IF_ERR(vm_obj_get_obj(&src, source));

  vm_resolved_t slot;
  SE_RET_IF_ERR(resolve_acc(target, true, &slot));
  SE_RET_IF_ERR(writable(slot.owner, user));
  if (unlikely(slot.payload.type != VM_OBJ_PTR || !slot.payload.ptr)) {
    return vm_err_expected_ptr(target->id, target->count, (uint8_t)slot.payload.type, slot.owner);
  }
  vm_obj_h* cell = (vm_obj_h*)slot.payload.ptr;

  /* The allocation-free case, and the one a running program is normally in:
     the destination already has the right shape, so nothing is built and this
     is exactly what a Set does. Allocation happens on the first pass, and
     afterwards only when the source's shape actually changes. */
  if (unlikely(!shape_matches(src, *cell, 0, true))) {
    vm_obj_h fresh = NULL;
    SE_RET_IF_ERR(clone_shape(&fresh, src, 0));
    // Fill before publication: src may live inside the tree being replaced.
    err_h e = copy_tree(vm_make_payload(src), vm_make_payload(fresh), fresh, 0, true, user);
    if (!e) e = slot_store(slot.owner, cell, fresh);
    if (e) vm_obj_dyn_release(fresh);
    return e;
  }

  SE_RET_IF_ERR(copy_tree(vm_make_payload(src), vm_make_payload(*cell), *cell, 0, false, user));
  SE_RET_IF_ERR(copy_tree(vm_make_payload(src), vm_make_payload(*cell), *cell, 0, true, user));
  slot.owner->head.f.upd = 1;  // a refilled snapshot is a publication too
  return NULL;
}

// ===========================================================================
// 2. Target Public APIs
// ===========================================================================

// --- Read APIs ---

err_h vm_obj_get_payload(vm_payload_t* target, const vm_accessor_t* source) {
  *target = (vm_payload_t){0};  // deterministic output on resolution failure
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(source, false, &r));
  *target = r.payload;
  return NULL;
}

err_h vm_obj_get_obj(vm_obj_h* target, const vm_accessor_t* source) {
  *target = NULL;
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(source, false, &r));

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

err_h vm_obj_get_owner(vm_obj_h* target, const vm_accessor_t* source) {
  *target = NULL;
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(source, false, &r));
  *target = r.owner;
  return NULL;
}

vm_obj_h vm_obj_get_child(vm_obj_h parent, const char* tag) {
  if (unlikely(!parent || !tag || (vm_obj_t_e)parent->head.d.obj_t != VM_OBJ_PTR)) return NULL;
  size_t len = strlen(tag);
  if (unlikely(len == 0 || len > VM_OBJ_NAME_MAX)) return NULL;
  int32_t idx = find_child_by_name(parent, tag, (uint8_t)len);
  if (idx < 0) return NULL;
  return ((vm_obj_h*)parent->payload)[idx];
}

// --- Write APIs ---

err_h vm_obj_set_scalar(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type) {
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(target, true, &r));
  return vm_internal_store_inline(r.owner, r.payload, v, src_type, target->id);
}

void vm_obj_clear_quiet(vm_obj_h obj) {
  if (!obj || !obj->head.f.mutable || (vm_obj_t_e)obj->head.d.obj_t == VM_OBJ_PTR) return;
  if (obj->head.payload_size) memset(obj->payload, 0, obj->head.payload_size);
}

// --- Copy & Clone APIs ---

err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target) {
  return copy_content(source, target, false);
}

bool vm_obj_shape_matches(vm_obj_h a, vm_obj_h b) {
  return shape_matches(a, b, 0, false);
}

bool vm_obj_schema_matches(vm_obj_h a, vm_obj_h b) {
  return shape_matches(a, b, 0, true);
}

err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src) {
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(src);
  return clone_shape(out, src, 0);
}

err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target) {
  return clone_into(source, target, false);
}

// --- Link APIs ---

err_h vm_obj_link(const vm_accessor_t* to_join, const vm_accessor_t* owner) {
  vm_obj_h child;
  SE_RET_IF_ERR(vm_obj_get_obj(&child, to_join));

  vm_resolved_t slot;
  SE_RET_IF_ERR(resolve_acc(owner, true, &slot));

  if (slot.payload.type != VM_OBJ_PTR || !slot.payload.ptr) {
    return vm_err_expected_ptr(owner->id, owner->count, (uint8_t)slot.payload.type, slot.owner);
  }

  return slot_store(slot.owner, (vm_obj_h*)slot.payload.ptr, child);
}

err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child) {
  SE_CHECK_NOT_NULL(cell);
  SE_CHECK_NOT_NULL(child);
  if (!cell->head.f.mutable) return vm_obj_not_mutable_err(cell);
  vm_payload_t slot = obj_elem(cell, index);
  if (!slot.ptr) return vm_obj_oob_err(cell, index);
  if (slot.type != VM_OBJ_PTR) return vm_obj_not_ptr_err(cell, slot.type);
  return slot_store(cell, (vm_obj_h*)slot.ptr, child);
}

// --- Publication APIs ---

err_h vm_obj_publish(vm_obj_h obj) {
  SE_RET_IF_ERR(writable(obj, false));
  obj->head.f.upd = 1;
  return NULL;
}

// --- User Mutation Boundaries (_usr) ---

err_h vm_obj_set_scalar_usr(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type) {
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(target, true, &r));
  SE_RET_IF_ERR(writable(r.owner, true));
  return vm_internal_store_inline(r.owner, r.payload, v, src_type, target->id);
}

err_h vm_obj_set_scalar_direct_usr(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type) {
  SE_RET_IF_ERR(writable(obj, true));
  return vm_obj_set_scalar_direct(obj, index, v, src_type);
}

err_h vm_obj_copy_content_usr(const vm_accessor_t* source, const vm_accessor_t* target) {
  return copy_content(source, target, true);
}

err_h vm_obj_clone_into_usr(const vm_accessor_t* source, const vm_accessor_t* target) {
  return clone_into(source, target, true);
}

err_h vm_obj_link_usr(const vm_accessor_t* child, const vm_accessor_t* target) {
  vm_resolved_t r;
  SE_RET_IF_ERR(resolve_acc(target, true, &r));
  SE_RET_IF_ERR(writable(r.owner, true));
  return vm_obj_link(child, target);
}

err_h vm_obj_publish_usr(vm_obj_h obj) {
  SE_RET_IF_ERR(writable(obj, true));
  obj->head.f.upd = 1;
  return NULL;
}
