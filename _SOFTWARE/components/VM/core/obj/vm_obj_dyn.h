#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "vm_obj.h"
#include "vm_obj_build.h"

// ===========================================================================
// 1. Constants & Types
// ===========================================================================

/**
 * @brief Live dynamic objects at once. A ceiling, not a budget -- the real
 * limit is the heap, and this exists so a runaway or malformed message is
 * rejected cleanly instead of consuming DRAM until something unrelated fails.
 */
#define VM_DYN_MAX 128
#define VM_DYN_MAX_DEPTH 16  // maximum dynamic nodes on an ownership path

#define VM_OWNERSHIP_CYCLE 0u
#define VM_OWNERSHIP_DEPTH 1u

/** @brief "Not in the register" -- what vm_obj_dyn_get_id() returns for an object
 *  that is not registered, including every arena object. */
#define VM_DYN_NO_ID 0xFFFFu

typedef struct vm_obj_dyn_meta_t {
  uint32_t ref_cnt;  // parents holding this object; freed at zero
  vm_obj_h obj;      // NULL marks a free slot -- the table is sparse
} vm_obj_dyn_meta_t;

/** @brief The register. Public only because vm_obj_dyn_get_id() and
 *  vm_obj_dyn_get_by_id() are inline; vm_obj_dyn.c owns every mutation of it. */
extern vm_obj_dyn_meta_t g_vm_dyn[VM_DYN_MAX];

// ===========================================================================
// 2. Helpers (Inspection & Registry Accessors)
// ===========================================================================

/** @brief Whether this object is heap-allocated and reference counted. Arena
 *  objects always answer false -- vm_obj_create() clears the flag and there is
 *  no cfg field that sets it. */
static inline bool vm_obj_is_dynamic(vm_obj_h o) {
  return o != NULL && o->head.f.dynamic != 0;
}

/**
 * @brief This object's slot in the dynamic register, or VM_DYN_NO_ID.
 *
 * Requires a live object handle (or NULL); never call with a released handle.
 * Found by comparing pointers, which is why nothing is bolted onto the object
 * itself: a dynamic object is byte-for-byte an ordinary vm_obj_t, and free()
 * takes the handle directly.
 *
 * The `dynamic` bit is tested first, and that test is what makes the register
 * safe to consult from a block body. Every object in a loaded program is an
 * arena object, so without it the common case is VM_DYN_MAX pointer compares
 * to conclude "not here" -- and a block that links on every pass would pay
 * that twice per call, inside the pass loop. With it, an arena object costs
 * one bit and the scan runs only for objects that can actually be in the
 * register.
 */
static inline uint16_t vm_obj_dyn_get_id(vm_obj_h o) {
  if (!o || !o->head.f.dynamic) return VM_DYN_NO_ID;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (g_vm_dyn[i].obj == o) return i;
  }
  return VM_DYN_NO_ID;
}

/** @brief Direct register access by ID, for teardown, telemetry and debug listings.
 *  NULL where the slot is empty -- the register is sparse, so a walk covers
 *  [0, VM_DYN_MAX) and skips holes rather than stopping at the first one. */
static inline vm_obj_h vm_obj_dyn_get_by_id(uint16_t id) {
  return (id < VM_DYN_MAX) ? g_vm_dyn[id].obj : NULL;
}

// ===========================================================================
// 3. Target Public APIs (Lifecycle, Retain, Release, Link Validation)
// ===========================================================================

/**
 * @brief Allocate a dynamic object and register it, reference count zero.
 *
 * Zero is deliberate: the object is owned by nothing until it is linked into a
 * parent, and linking is what takes the first reference. Allocate and link
 * within one block body so that window never spans a pass.
 *
 * Takes the same vm_obj_head_t as vm_obj_create(), so a shape read off an
 * existing object can be handed straight back in -- which is what re-parsing a
 * message into a fresh tree needs.
 *
 * @return err_h every rejection vm_obj_create() raises for the same head,
 *         ERR_VM_DYN_FULL when the register already holds VM_DYN_MAX objects,
 *         or ERR_BASE_NO_MEM when the heap cannot supply the bytes.
 */
err_h vm_obj_dyn_create(vm_obj_h* out, const vm_obj_head_t* head, const char* name);

/** @brief Take a reference -- called when the object is stored into a parent's
 *  pointer slot. No-op on an arena object, so link paths need no type test. */
void vm_obj_dyn_retain(vm_obj_h o);

/** @brief Validate the proposed pointer-slot replacement without mutating it.
 * Dynamic-to-dynamic edges own references and must form a bounded DAG. Arena
 * edges are independent program-lifetime roots, not recursive ownership. */
err_h vm_obj_dyn_check_link(vm_obj_h owner, vm_obj_h* cell, vm_obj_h child);

/** @brief Drop a reference -- called when a parent's slot stops pointing here.
 *  At zero the object leaves the register and is freed, releasing its own
 *  dynamic children as it goes and leaving arena children alone. No-op on an
 *  arena object. The only correct way to dispose of a dynamic object. */
void vm_obj_dyn_release(vm_obj_h o);

/** @brief Allocator teardown primitive, called by vm_store_reset() before
 *  releasing the arena. Invalidates every dynamic handle, regardless of count.
 *  Runtime callers use vm_loader_reset(), which also quiesces execution. */
void vm_obj_dyn_reset(void);
