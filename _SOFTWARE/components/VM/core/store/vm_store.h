#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"

/*
The loaded program's storage, in one place: one bump arena, plus one id ->
pointer registry per kind of thing a program contains (objects, accessors,
blocks). Those three used to be separate table structs with duplicated
build/set/reset code; now they're thin typed wrappers over this.

Usage, always in this order:

  vm_store_open(total, counts)   size the arena, build the registries
  vm_store_alloc(&p, r, id, n)   carve a chunk, zero it, bind it to an id
  vm_store_get(r, id)            id -> pointer, NULL past the end
  vm_store_reset()               detach every registry, then reset the arena

Nothing is freed individually. A program is built once and replaced whole, so
a bump allocator fits and reset is safe: no outstanding handles survive that
weren't already part of the program being discarded.
*/

/**
 * @brief Hard ceiling on one program's storage. A sanity bound, not a budget
 * -- the real limit is whatever contiguous internal DRAM is free at load time
 * (see vm_store_open).
 *
 * Sizing reference:
 *   block      16 B (vm_block_data_t) + 4 B per input + 4 B per output
 *              + 4 B per enable source + own vars
 *   object     4 B head + payload, 4-byte aligned, + tag bytes if named
 *   accessor   20 B + 8 B per chain index
 *   section    4 B (a [start, end) pair over the block order)
 *   registry   4 B per id, in each of the four id spaces
 */
#define VM_STORE_MAX_POOL (128 * 1024)

/** @brief Which registry. Also the `kind` reported by ERR_VM_REG_*. */
typedef enum vm_reg_e {
  VM_REG_OBJ = 0,
  VM_REG_ACC = 1,
  VM_REG_BLK = 2,
  /* Sections are `[start, end)` ranges over the block registry, which is
     itself the execution order (see [[VM_EXEC.MD]]). They get a registry of
     their own rather than a side table because they are allocated, bound and
     torn down on exactly the same schedule as everything else a program owns
     -- and reusing the registry means a bad section id already reports as
     ERR_VM_REG_OOB with the right `kind`. */
  VM_REG_SEC = 3,
  VM_REG_CNT = 4,
} vm_reg_e;

/** @brief "Allocate but do not bind" -- for storage addressed through
 *  something else (e.g. an accessor's name bytes) or built at runtime. */
#define VM_ID_NONE 0xFFFFu

/** @brief The bump region a program's storage is carved out of. Public only
 *  because vm_store_t embeds it and vm_store_get() is inline; carving itself
 *  is private to vm_store.c. */
typedef struct vm_alloc_t {
  uint8_t* base;
  uint32_t capacity;
  uint32_t offset;  // bytes handed out; only ever grows until the pool is replaced
} vm_alloc_t;

typedef struct vm_registry_t {
  void** items;    // id -> pointer; every slot starts NULL
  uint16_t count;  // ids in range [0, count)
} vm_registry_t;

typedef struct vm_store_t {
  vm_alloc_t arena;
  vm_registry_t reg[VM_REG_CNT];
} vm_store_t;

extern vm_store_t g_vm_store;

/** @brief id -> pointer, NULL past the end of the loaded program. */
static inline void* vm_store_get(vm_reg_e r, uint16_t id) {
  const vm_registry_t* g = &g_vm_store.reg[r];
  return (id < g->count) ? g->items[id] : NULL;
}

/**
 * @brief Reclaim dynamic objects, detach every registry, then free the arena.
 * Requires quiescent execution: use vm_loader_reset() from firmware control
 * code, which waits for passes and clears subscriptions/runtime state as well.
 * Every raw handle and cached accessor expires; only fresh ID lookups are safe.
 */
void vm_store_reset(void);

/**
 * @brief Allocate a pool of exactly @p total_size, then build the registries.
 *
 * Pool is heap-allocated per load (not reserved statically), and the new
 * pool is allocated **before** the old one is freed; registry capacity is
 * validated first. Failure leaves old storage intact. This low-level API
 * requires quiescent execution; firmware uses vm_loader_open().
 *
 * @param total_size Bytes the program says it needs, registries included.
 * @param counts One id count per vm_reg_e, in enum order. Zero is legal.
 * @return err_h ERR_VM_LOAD_TOO_BIG if total_size exceeds VM_STORE_MAX_POOL
 *         or no contiguous block that size exists (payload carries requested
 *         and largest-available), ERR_BASE_NO_MEM if the registries don't
 *         fit inside the pool the program asked for.
 */
err_h vm_store_open(uint32_t total_size, const uint16_t counts[VM_REG_CNT]);

/**
 * @brief Carve a zeroed, 4-aligned chunk and bind it to @p id.
 *
 * The id is validated before the chunk is carved, so a bad id spends no
 * arena and its retry still fits.
 *
 * @param out Receives the chunk; set to NULL on any failure.
 * @param r Registry to bind in; ignored when id is VM_ID_NONE.
 * @param id VM_ID_NONE to allocate without binding.
 * @return err_h ERR_VM_REG_OOB if id is past the registry, ERR_VM_REG_DUP if
 *         the slot is already bound, ERR_BASE_NO_MEM if the arena is full.
 */
err_h vm_store_alloc(void** out, vm_reg_e r, uint16_t id, uint32_t size);

/** @brief Bytes consumed so far, for diagnostics after a load. */
uint32_t vm_store_used(void);

/** @brief Bytes the arena was opened with. */
uint32_t vm_store_capacity(void);
