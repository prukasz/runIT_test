#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"

#define VM_STORE_MAX_POOL (128 * 1024)  // Hard ceiling on program bump arena
#define VM_ID_NONE        0xFFFFu       // Allocate without binding to registry

typedef enum vm_reg_e {
  VM_REG_OBJ = 0,
  VM_REG_ACC = 1,
  VM_REG_BLK = 2,
  VM_REG_CNT = 3,
} vm_reg_e;

typedef struct vm_alloc_t {
  uint8_t* base;
  uint32_t capacity;
  uint32_t offset;
} vm_alloc_t;

typedef struct vm_registry_t {
  void**   items;  // id -> pointer table
  uint16_t count;  // ids in range [0, count)
} vm_registry_t;

typedef struct vm_store_t {
  vm_alloc_t    arena;
  vm_registry_t reg[VM_REG_CNT];
} vm_store_t;

extern vm_store_t g_vm_store;

/** @brief Look up pointer by registry and ID; returns NULL if out of bounds. */
static inline void* vm_store_get(vm_reg_e r, uint16_t id) {
  const vm_registry_t* g = &g_vm_store.reg[r];
  return (id < g->count) ? g->items[id] : NULL;
}

/** @brief Free dynamic objects, detach registries, and release arena memory. */
void vm_store_reset(void);

/**
 * @brief Allocate arena pool and initialize object/accessor/block registries.
 * @param total_size Total arena bytes requested.
 * @param counts Item counts for each vm_reg_e registry.
 */
err_h vm_store_open(uint32_t total_size, const uint16_t counts[VM_REG_CNT]);

/**
 * @brief Carve a zeroed, 4-aligned chunk from arena and bind to registry ID.
 * @param out Receives allocated pointer.
 * @param r Target registry (ignored if id == VM_ID_NONE).
 * @param id Registry ID or VM_ID_NONE.
 * @param size Allocation size in bytes.
 */
err_h vm_store_alloc(void** out, vm_reg_e r, uint16_t id, uint32_t size);

/** @brief Current bytes allocated from the arena. */
uint32_t vm_store_used(void);

/** @brief Total arena capacity in bytes. */
uint32_t vm_store_capacity(void);
