#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_obj.h"
#include "vm_store.h"

/*
Object access layer. Three levels, in increasing order of "how much does the
caller already know":

  vm_obj_get_payload()  -> where the value lives (type + ptr + count)
  VM_OBJ_GET_VAL()      -> one converted scalar, straight into a local
  vm_get_obj()          -> the object handle itself, header and all

Everything is addressed through vm_accessor_t (id + optional index chain) and
resolved live against the loaded program's object table. Failures come back
as err_h chains, never silent sentinels -- see [[SYS_ERRORS.MD]]; the caller
decides whether to propagate (SE_RET_IF_ERR) or report (BLOCK_CALL /
SE_ORIGIN_CALL).

Hot-path shape: vm_resolve_fast() (inline, in this header) handles the common
accessor shapes without a walk; resolve_d() (vm_obj_access.c) is the general
fallback. Every entry point tries fast first and only walks on a miss.
*/

// ---------------------------------------------------------------------------
// id -> object, over the shared registry. One program is loaded at a time
// (storage rebuilt whole on reload, see vm_store.h), so accessors resolve
// against the global store rather than threading a table pointer everywhere.
// ---------------------------------------------------------------------------

static __always_inline vm_obj_h vm_obj_by_id(uint16_t id) {
  return (vm_obj_h)vm_store_get(VM_REG_OBJ, id);
}

/** @brief Where a value lives: address in the arena, element count, type.
 *  Packed into exactly 8 bytes (2 words / 64-bit register pair). */
typedef struct vm_payload_t {
  void* ptr;       // NULL when unresolved (4 bytes)
  uint16_t count;  // number of `type` elements available at ptr (2 bytes)
  uint8_t type;    // vm_obj_t_e (1 byte)
  uint8_t _pad;    // (1 byte)
} vm_payload_t;

_Static_assert(sizeof(vm_payload_t) == 8, "vm_payload_t must be 8 bytes");

/** @brief Scratch value wide enough for any scalar vm_obj_t_e. Untagged on
 *  its own -- the payload's type travels alongside it. */
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
// vm_accessor_t -- id + chain of indices, e.g. object(id, [0][1])
// ---------------------------------------------------------------------------

typedef struct vm_accessor_t vm_accessor_t;

typedef enum vm_index_kind_e {
  VM_IDX_LITERAL = 0,  // fixed position, known at compile time
  VM_IDX_REF = 1,      // resolve another accessor and read it as the index
  VM_IDX_NAME = 2,     // match a child object's tag
} vm_index_kind_e;

/*
One step of a chain:

  LITERAL  object(id, [2])            fixed position
  REF      object(id, [ object(9) ])  position read live from another object
                                       (a selector wire, a sequencer's step)
  NAME     object(id, ["temp"])       the child tagged "temp"

NAME is for data whose field *order* isn't known at compile time -- a parsed
message, where "temp" may land anywhere. Only works on a VM_OBJ_PTR parent
with tagged children (vm_obj_tag()); costs a scan with a compare each, fine
for a handful of fields, wrong for a compiled path where LITERAL applies.

`name` is a bare pointer (NUL-terminated, must outlive the accessor), not an
inline buffer, so LITERAL indices pay nothing for the feature existing.
`name_len` rides in padding `kind` already wasted -- authoritative, the scan
never re-measures the string.
*/
typedef struct {
  uint8_t kind;      // vm_index_kind_e
  uint8_t name_len;  // VM_IDX_NAME only: strlen(name), measured once at build
  union {
    uint32_t value;            // VM_IDX_LITERAL
    const vm_accessor_t* ref;  // VM_IDX_REF
    const char* name;          // VM_IDX_NAME
  };
} vm_index_t;

/**
 * @brief Static initialiser for a by-name index: `VM_IDX_BY_NAME("temp")`.
 * String literals only (sizeof, not strlen) -- a hand-written struct with
 * the wrong name_len matches nothing. vm_accessor_set_name() is the runtime
 * equivalent.
 */
#define VM_IDX_BY_NAME(str) \
  { .kind = VM_IDX_NAME, .name_len = (uint8_t)(sizeof(str) - 1), .name = (str) }

/*
Resolution cache -- set by vm_accessor_cache_build() once the program loads,
cleared with the rest of the accessor on reload.

Only shapes whose *address* can't move are cached: a whole-object accessor,
or a single literal index on a root object (neither traverses a VM_OBJ_PTR).
A single literal index on a PTR object is cached too, safely: what's cached
is the pointer *slot*'s address, not the object behind it -- a chain that
follows the link (vm_get_obj()) still dereferences it live, so re-linking is
picked up exactly as before. That's why this tier needs no invalidation.

Anything deeper (`obj(id, [j][k])`) is deliberately not cached: its address
depends on a link vm_obj_link() can move under it. The cache is an
optimisation only -- an unbuilt or disqualified accessor just resolves long.
*/
#define VM_ACC_F_CACHED 0x01u

/*
`indices` points into the accessor's own tail rather than being a flexible
array member -- the one place the three registries (obj/acc/block) differ in
how they express "header + trailing bytes", even though they're all carved as
a single chunk. A FAM was tried and reverted: C11 6.7.2.1p3 forbids a struct
with a FAM from being a struct member, which is what a compile-time `static
const` accessor needs. Costs 4 bytes and one dependent load per chain step;
the resolution cache already removes that load from every hot shape.
*/
struct vm_accessor_t {
  uint16_t id;                // root object's id in the registry
  uint8_t count;              // number of chained indices; 0 = whole object
  uint8_t flags;              // VM_ACC_F_*
  const vm_index_t* indices;  // `count` entries, allocated in the same chunk
  vm_obj_h c_owner;           // cache: object the value lives in
  vm_payload_t c_payload;     // cache: resolved payload (ptr, count, type, pad)
};

_Static_assert(sizeof(struct vm_accessor_t) == 20, "accessor header size feeds the RAM budget in VM.MD");

/*
Accessors get their own id space so an upload can name one and point several
things at it -- sharing is the expected case (ten blocks reading the same
object share one accessor). Blocks hold resolved `vm_accessor_t*`, not ids:
ids are the wire form, pointers the runtime form, resolved once at load. That
makes accessors ordering-dependent in an upload, same as linked objects.
*/
/** @brief id -> accessor, NULL past the end of the loaded program. */
static __always_inline vm_accessor_t* vm_accessor_by_id(uint16_t id) {
  return (vm_accessor_t*)vm_store_get(VM_REG_ACC, id);
}

// caps by_ref recursion: a packet-sourced tree isn't trusted to be acyclic
#define VM_ACCESSOR_MAX_DEPTH 8

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

/** @brief What a walk produces: what it landed on, and which object those
 *  bytes belong to. Callers want one or the other; only the walk has both. */
typedef struct vm_resolved_t {
  vm_payload_t payload;
  vm_obj_h owner;  // object the payload's bytes belong to
} vm_resolved_t;

/**
 * @brief Resolve the accessor shapes worth not walking for, entirely inline:
 * `obj(id)`, `obj(id, [k])`, `obj(id, [j][k])` with literal indices -- nearly
 * every wire a compiled program has.
 *
 * Returns false for a shape it doesn't handle *and* for every failure, never
 * an error of its own -- callers fall through to resolve_d(), which re-derives
 * the chain and builds the real err_h. Failure is cold, so paying for the
 * walk twice there is cheaper than carrying error construction through here.
 *
 * Lives in the header so a block's pin read is straight-line code, not a
 * call -- the resolved type/pointer stay in registers instead of round-
 * tripping through a returned struct. Largest single cost in an access; see
 * the benchmark section of VM.MD.
 *
 * Must stay in agreement with resolve_d() about what it accepts, in
 * particular which object (the deepest one) the mutability check runs on.
 */
static __always_inline bool vm_resolve_fast(const vm_accessor_t* acc, bool for_write, vm_resolved_t* out) {
  // pre-resolved at load: four loads, no walk. Mutability is still read
  // live (not baked into flags) so a future "revoke write access" feature
  // can't be silently outlived by the cache.
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
// Out-of-line entry points (vm_obj_access.c -- ambient OWNER for SE_* macros)
// ---------------------------------------------------------------------------

/** @brief Resolve `source` to where its value lives. Read path: no mutability check. */
err_h vm_obj_get_payload(vm_payload_t* target, const vm_accessor_t* source);

/** @brief Resolve `source` to the deepest object it reaches, ignoring the
 *  payload -- for blocks that forward or inspect a whole object (relay,
 *  encoder, demux consumer). A chain ending on a VM_OBJ_PTR element yields the
 *  object it points at; a chainless accessor (count 0) yields the named
 *  object itself, PTR container included. */
err_h vm_get_obj(vm_obj_h* target, const vm_accessor_t* source);

/**
 * @brief Find a child object by tag on a VM_OBJ_PTR container directly.
 *
 * @param parent A VM_OBJ_PTR container object.
 * @param tag NUL-terminated tag string to look for.
 * @return vm_obj_h The child object handle, or NULL if not found or parent is not VM_OBJ_PTR.
 */
vm_obj_h vm_obj_find_child(vm_obj_h parent, const char* tag);

/** @brief Copy value bytes from one object into another. Both must resolve to
 *  the same element type and count, else ERR_VM_OBJ_COPY_MISMATCH. Refuses
 *  non-mutable targets and VM_OBJ_PTR (link instead -- raw pointer bytes are
 *  meaningless outside their own graph). */
err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);

/** @brief Point a VM_OBJ_PTR slot at an object: `owner` must resolve to a
 *  pointer element, `to_join` to the object to store there. The switch/mux/
 *  demux primitive -- rewires which object a consumer's chain lands on. */
err_h vm_obj_link(const vm_accessor_t* to_join, const vm_accessor_t* owner);

/** @brief Write a scalar into `target`, converting to whatever type is
 *  actually stored. Checks mutability, sets `upd`. Prefer VM_OBJ_SET_VAL(). */
err_h vm_obj_set_scalar(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type);

/** @brief Cold half of vm_store_inline(): builds the "target is not a
 *  scalar" error. Out of line for the ambient OWNER a header can't have. */
err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id);

/*
Two write paths, because `mutable` and `usr_mutable` guard different things.

`mutable` is "may be written at all" -- a constant is not. `usr_mutable` is the
narrower one: "may be written by *user logic*". A block's own ENO or result is
mutable, because the block that owns it writes it every pass, and not
usr_mutable, because a Set block the user drew must not be able to reach in and
forge another block's flow.

The flag has existed in the header and travelled over the wire
(VM_LOAD_F_USR_MUTABLE) since objects did; what was missing was anyone reading
it. Enforcing it means the write path has to know who is asking, which is
exactly what these two entry points are: internal writes (a block publishing
its own output) keep going through vm_obj_set_scalar() and check `mutable`
only, while user-authored and remote writes go through the _usr forms and
check both.

Not inlined, and not fast: a live patch or a user-drawn Set block runs once,
not once per element, so the extra load is free where it lands.
*/

/** @brief Cold arm of the user-write check. Out of line for the ambient OWNER. */
err_h vm_obj_not_usr_mutable_err(vm_obj_h obj, uint16_t id);

/** @brief vm_obj_set_scalar() for a user-authored or remote write: rejects a
 *  target the program marked off-limits to user logic. */
err_h vm_obj_set_scalar_usr(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type);

/** @brief vm_obj_copy_content() for a user-authored or remote write. */
err_h vm_obj_copy_content_usr(const vm_accessor_t* source, const vm_accessor_t* target);

/**
 * @brief Zero an object's payload without touching its header.
 *
 * The "quiet write" the non-activation path needs: a clear that set `upd`
 * would look like new data to any update-driven block downstream, and one
 * event would ripple outward for a second pass. Nothing here reads or writes
 * head.f at all, so there is no window in which it briefly says otherwise.
 *
 * Refuses VM_OBJ_PTR: its payload is a link, not a value, and zeroing one
 * would drop a reference vm_obj_dyn_release() never sees.
 */
void vm_obj_clear_quiet(vm_obj_h obj);

/*
Direct-object write path, for a block publishing to an output it owns: an
output is bound once at load and never moves, so the output slot holds the
vm_obj_h itself and skips the accessor walk. Still checks mutable, still sets
upd -- only the address lookup is dropped.
*/

// cold arms, out of line: ambient OWNER, and none run on a working program
err_h vm_obj_null_obj_err(void);
err_h vm_obj_not_mutable_err(vm_obj_h obj);
err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index);

/** @brief Write a scalar directly into `obj`'s element `index`. Inline: every
 *  block publishes a result this way, every cycle -- see vm_store_inline(). */
static __always_inline err_h vm_obj_set_scalar_direct(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type);

/** @brief Point `cell`'s pointer element `index` at `child` -- vm_obj_link()
 *  without the accessor walk, for a switch/demux writing its own cell. */
err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child);

// ---------------------------------------------------------------------------
// Value conversion
// ---------------------------------------------------------------------------

// Reads a resolved payload's first element into the union member matching its
// stored type. PTR/NONE aren't scalars, left zeroed. VM_OBJ_STR is an array
// of chars, so one *element* of it is a byte like any other.
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

/*
Casts a vm_val_t (typed by `type`) into *dst_ptr. __typeof__ picks the target
type from dst_ptr, so this works for any scalar destination with one macro;
the inner _Generic skips needless rounding on float->float.

Range is NOT clamped to the destination -- narrowing a 300 into a uint8_t
still wraps. Clamp in a wide type first if that matters (get into float,
clamp, then narrow).
*/
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

/*
Read conversion as three out-of-line functions rather than a switch pasted
into every call site. Fusing load+convert inline was right for speed, wrong
for size: the six arms (float->int64 saturation, U64->float pulling in
__floatundisf) cost ~450 B *per pin* -- a 50-type palette hit ~90 kB, thrashing
the 16 kB instruction cache and slowing every block, not just the big ones.

Splitting on the *destination* keeps the fast half inlined: only three
destination shapes exist (float, <=32-bit int, 64-bit int), so three shared
functions cover the whole program and each call site shrinks to a resolve
plus a call. vm_resolve_fast() stays inline, which is where the speed
actually came from.

`src` must be non-NULL and point at a resolved element of the given type.
*/
float vm_read_as_f32(vm_obj_t_e type, const void* src);
int32_t vm_read_as_i32(vm_obj_t_e type, const void* src);
int64_t vm_read_as_i64(vm_obj_t_e type, const void* src);

// Picks the converter from the destination's type, then narrows. <=32-bit
// integer destinations all route through vm_read_as_i32 and cast -- exact in
// two's complement. Not clamped to the destination, same as VM_VAL_CAST_TO.
// Fast-path: matching types read directly without an out-of-line call.
#define VM_LOAD_CAST_TO(dst_ptr, type, src)                                                        \
  do {                                                                                             \
    const void* __lc_s = (const void*)(src);                                                       \
    vm_obj_t_e __lc_t = (type);                                                                    \
    *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr),                                     \
        float: (likely(__lc_t == VM_OBJ_F)                                                         \
                   ? *(const float*)__lc_s                                                         \
                   : vm_read_as_f32(__lc_t, __lc_s)),                                              \
        double: (likely(__lc_t == VM_OBJ_F)                                                        \
                    ? (double)*(const float*)__lc_s                                                \
                    : (double)vm_read_as_f32(__lc_t, __lc_s)),                                     \
        int64_t: (likely(__lc_t == VM_OBJ_U64)                                                     \
                     ? *(const int64_t*)__lc_s                                                     \
                     : vm_read_as_i64(__lc_t, __lc_s)),                                            \
        uint64_t: (likely(__lc_t == VM_OBJ_U64)                                                    \
                      ? *(const uint64_t*)__lc_s                                                   \
                      : (uint64_t)vm_read_as_i64(__lc_t, __lc_s)),                                 \
        int32_t: (likely(__lc_t == VM_OBJ_I32 || __lc_t == VM_OBJ_U32)                            \
                     ? *(const int32_t*)__lc_s                                                     \
                     : vm_read_as_i32(__lc_t, __lc_s)),                                            \
        uint32_t: (likely(__lc_t == VM_OBJ_U32 || __lc_t == VM_OBJ_I32)                           \
                      ? *(const uint32_t*)__lc_s                                                   \
                      : (uint32_t)vm_read_as_i32(__lc_t, __lc_s)),                                 \
        default: vm_read_as_i32(__lc_t, __lc_s));                                                  \
  } while (0)

// Same conversion, inlined rather than called. VM_LOAD_CAST_TO trades ~10 cyc
// for ~200 B per *site*, right for hundreds of once-per-cycle pin reads; this
// inverts that for the handful of sites (fold, copy, encoder) that run the
// conversion once per *element* with the type loop-invariant -- measured 80%
// slower (21.7 -> 39.1 cyc/element) routed through the shared converters.
// Use VM_OBJ_GET_VAL normally; reach for this only inside an element loop.
#define VM_LOAD_CAST_TO_FAST(dst_ptr, type, src)                                                                                 \
  do {                                                                                                                           \
    const void* __lf_s = (const void*)(src);                                                                                      \
    switch (type) {                                                                                                              \
      case VM_OBJ_U8:                                                                                                            \
      case VM_OBJ_B:                                                                                                             \
      case VM_OBJ_STR:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr))) * (const uint8_t*)__lf_s;                                                           \
        break;                                                                                                                   \
      case VM_OBJ_U32:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr))) * (const uint32_t*)__lf_s;                                                          \
        break;                                                                                                                   \
      case VM_OBJ_I32:                                                                                                           \
        *(dst_ptr) = (__typeof__(*(dst_ptr))) * (const int32_t*)__lf_s;                                                           \
        break;                                                                                                                   \
      case VM_OBJ_U64: {                                                                                                         \
        uint64_t __lf_v;                                                                                                          \
        memcpy(&__lf_v, __lf_s, sizeof(__lf_v));                                                                                  \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))__lf_v;                                                                              \
        break;                                                                                                                   \
      }                                                                                                                          \
      case VM_OBJ_F: {                                                                                                           \
        float __lf_f = *(const float*)__lf_s;                                                                                     \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))_Generic(*(dst_ptr), float: __lf_f, double: __lf_f, default: vm_f_to_i(__lf_f));    \
        break;                                                                                                                   \
      }                                                                                                                          \
      default:                                                                                                                   \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))0;                                                                                   \
        break;                                                                                                                   \
    }                                                                                                                            \
  } while (0)

/**
 * @brief Convert `v` into whatever `slot` actually stores, then mark `owner`
 *        updated. The shared tail of every scalar write.
 *
 * Inline like vm_resolve_fast(): this is what a block does to publish a
 * result, and a call here would cost more than the store itself. Mutability
 * is the caller's to check, at the resolve site.
 */
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
    case VM_OBJ_STR:  // one element of a char array is a byte
      VM_VAL_CAST_TO((uint8_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_U32:
      VM_VAL_CAST_TO((uint32_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_I32:
      VM_VAL_CAST_TO((int32_t*)slot.ptr, v, src_type);
      break;
    case VM_OBJ_U64: {
      uint64_t tmp;  // local + memcpy: see the alignment note in vm_payload_read()
      VM_VAL_CAST_TO(&tmp, v, src_type);
      memcpy(slot.ptr, &tmp, sizeof(tmp));
      break;
    }
    case VM_OBJ_F:
      VM_VAL_CAST_TO((float*)slot.ptr, v, src_type);
      break;
    default:
      return vm_obj_not_scalar_err(owner, slot.type, err_id);  // PTR/NONE aren't scalars -- use vm_obj_link() / vm_obj_copy_content()
  }
  owner->head.f.upd = 1;
  return NULL;
}

static __always_inline err_h vm_obj_set_scalar_direct(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type) {
  if (unlikely(obj == NULL)) return vm_obj_null_obj_err();
  if (unlikely(!obj->head.f.mutable)) return vm_obj_not_mutable_err(obj);
  uint8_t* p = vm_obj_elem_ptr(obj, index);
  if (unlikely(p == NULL)) return vm_obj_oob_err(obj, index);
  return vm_store_inline(obj, (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0}, v, src_type, 0);
}

// Types a caller's C value with the matching vm_obj_t_e, for the SET path.
// `char` gets its own arm because C makes it distinct from int8_t/uint8_t --
// without one, a plain `char` would fall to default and write 0, exactly
// wrong for a VM_OBJ_STR (array of chars).
#define VM_TYPE_OF(x) _Generic((x), uint8_t: VM_OBJ_U8, int8_t: VM_OBJ_I32, char: VM_OBJ_U8, uint16_t: VM_OBJ_U32, int16_t: VM_OBJ_I32, uint32_t: VM_OBJ_U32, int32_t: VM_OBJ_I32, uint64_t: VM_OBJ_U64, int64_t: VM_OBJ_U64, float: VM_OBJ_F, double: VM_OBJ_F, bool: VM_OBJ_B, default: VM_OBJ_NONE)

// Every arm must write the union member VM_TYPE_OF's answer is read back
// through (U8/B read as .u8, so must write .u8 -- falling to .u32 happens to
// work little-endian, but that's byte-order luck, not a contract). `char`
// goes through uint8_t since plain char's signedness is implementation-defined.
#define VM_VAL_OF(x)                                 \
  _Generic((x),                                      \
      float: (vm_val_t){.f = (float)(x)},            \
      double: (vm_val_t){.f = (float)(x)},           \
      bool: (vm_val_t){.u8 = (uint8_t)!!(x)},        \
      uint8_t: (vm_val_t){.u8 = (uint8_t)(x)},       \
      char: (vm_val_t){.u8 = (uint8_t)(x)},          \
      int8_t: (vm_val_t){.i32 = (int32_t)(x)},       \
      int16_t: (vm_val_t){.i32 = (int32_t)(x)},      \
      int32_t: (vm_val_t){.i32 = (int32_t)(x)},      \
      uint64_t: (vm_val_t){.u64 = (uint64_t)(x)},    \
      int64_t: (vm_val_t){.u64 = (uint64_t)(x)},     \
      default: (vm_val_t){.u32 = (uint32_t)(x)})

/**
 * @brief Read one converted scalar out of `source` into the local `output`.
 *
 * Takes an lvalue, evaluates to err_h (NULL on success, output untouched on
 * failure). The caller's declared type drives the conversion.
 *
 * @code
 * float angle = 0;
 * BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), block);
 * if (angle > 180.0f) angle = 180.0f;   // clamp wide, then narrow
 * @endcode
 */
#define VM_OBJ_GET_VAL(output, source)                                        \
  ({                                                                          \
    const vm_accessor_t* __gv_a = (source);                                   \
    vm_resolved_t __gv_r;                                                     \
    err_h __gv_e = NULL;                                                      \
    const void* __gv_ptr;                                                     \
    vm_obj_t_e __gv_t;                                                        \
    if (likely(vm_resolve_fast(__gv_a, false, &__gv_r))) {                    \
      __gv_ptr = __gv_r.payload.ptr;                                          \
      __gv_t = (vm_obj_t_e)__gv_r.payload.type;                               \
    } else {                                                                  \
      vm_payload_t __gv_p;                                                    \
      __gv_e = vm_obj_get_payload(&__gv_p, __gv_a);                           \
      __gv_ptr = __gv_p.ptr;                                                  \
      __gv_t = (vm_obj_t_e)__gv_p.type;                                       \
    }                                                                         \
    if (likely(!__gv_e)) {                                                    \
      VM_LOAD_CAST_TO(&(output), __gv_t, __gv_ptr);                           \
    }                                                                         \
    __gv_e;                                                                   \
  })

/**
 * @brief Write scalar `source` into `target`, converting to the stored type.
 *
 * Evaluates to err_h. Float sources are rounded and saturated into an
 * integer object (vm_f_to_i), so 179.6 lands on 180, not truncated or UB.
 *
 * @code
 * uint64_t sum = ...;
 * BLOCK_CALL(VM_OBJ_SET_VAL(sum, out0), block);
 * @endcode
 */
#define VM_OBJ_SET_VAL(source, target)                                                   \
  ({                                                                                     \
    const vm_accessor_t* __sv_a = (target);                                              \
    vm_val_t __sv_v = VM_VAL_OF(source);                                                 \
    vm_obj_t_e __sv_t = VM_TYPE_OF(source);                                              \
    vm_resolved_t __sv_r;                                                                \
    likely(vm_resolve_fast(__sv_a, true, &__sv_r))                                       \
        ? vm_store_inline(__sv_r.owner, __sv_r.payload, __sv_v, __sv_t, __sv_a->id)      \
        : vm_obj_set_scalar(__sv_a, __sv_v, __sv_t);                                     \
  })

/**
 * @brief Write scalar `source` straight into an owned output object.
 *
 * The output-pin counterpart to VM_OBJ_SET_VAL: same conversion, no accessor
 * walk, since a block's own output is bound at load time and can't move.
 *
 * @code
 * uint64_t sum = ...;
 * BLOCK_CALL(VM_OBJ_SET_VAL_AT(sum, out0, 0), block);
 * @endcode
 */
#define VM_OBJ_SET_VAL_AT(source, obj, index) vm_obj_set_scalar_direct((obj), (index), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief VM_OBJ_SET_VAL() for a user-authored or remote write -- checks
 *  `usr_mutable` on top of `mutable`. See the note above vm_obj_set_scalar_usr(). */
#define VM_OBJ_SET_VAL_USR(source, target) vm_obj_set_scalar_usr((target), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief VM_OBJ_SET_VAL_AT() for a user-authored or remote write. */
static __always_inline err_h vm_obj_set_scalar_direct_usr(vm_obj_h obj, uint16_t index, vm_val_t v, vm_obj_t_e src_type) {
  if (unlikely(obj == NULL)) return vm_obj_null_obj_err();
  if (unlikely(!obj->head.f.usr_mutable)) return vm_obj_not_usr_mutable_err(obj, 0);
  return vm_obj_set_scalar_direct(obj, index, v, src_type);
}

#define VM_OBJ_SET_VAL_AT_USR(source, obj, index) vm_obj_set_scalar_direct_usr((obj), (index), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief Read one converted scalar out of an already-resolved payload --
 *  the array-iteration counterpart to VM_OBJ_GET_VAL, no chain walk. */
#define VM_PAYLOAD_GET_VAL(output, payload)                     \
  do {                                                          \
    vm_payload_t __pv_p = (payload);                            \
    if (likely(__pv_p.ptr != NULL)) {                           \
      VM_LOAD_CAST_TO_FAST(&(output), __pv_p.type, __pv_p.ptr); \
    } else {                                                    \
      (output) = (__typeof__(output))0;                         \
    }                                                            \
  } while (0)
