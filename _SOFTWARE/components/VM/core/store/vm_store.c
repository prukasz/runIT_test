#include "vm_store.h"
#include <string.h>
#include "esp_compiler.h"
#include "esp_heap_caps.h"
#include "vm_obj_dyn.h"

#define OWNER OWNER_VM_STORE

static uint8_t* s_pool = NULL;  // current program's backing buffer; NULL when nothing is loaded

vm_store_t g_vm_store = {0};

// Internal DRAM only: no PSRAM on this board, and storage is touched every
// scan cycle, so a cache-dependent access cost is not wanted anyway.
#define VM_STORE_HEAP_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)

static void arena_init(vm_alloc_t* a, void* mem, uint32_t capacity) {
  a->base = (uint8_t*)mem;
  a->capacity = capacity;
  a->offset = 0;
}

// Bump-carve `size` bytes at 4-byte alignment. Bound is written as
// `aligned > capacity - size` (not `aligned + size > capacity`) so it can't
// overflow for a size that came off the wire.
static void* arena_carve(vm_alloc_t* a, uint32_t size) {
  uint32_t aligned = (a->offset + 3u) & ~3u;
  if (size > a->capacity || aligned > a->capacity - size) {
    SE_EMIT_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = size, .remaining = a->capacity - a->offset);
    return NULL;
  }
  a->offset = aligned + size;
  return a->base + aligned;
}

void vm_store_reset(void) {
  // The store owns both allocation domains. Never leave heap objects from an
  // old program behind when arena handles and accessor caches are discarded.
  vm_obj_dyn_reset();
  for (int r = 0; r < VM_REG_CNT; r++) {
    g_vm_store.reg[r].items = NULL;
    g_vm_store.reg[r].count = 0;
  }
  arena_init(&g_vm_store.arena, NULL, 0);

  heap_caps_free(s_pool);  // no-op on NULL
  s_pool = NULL;
}

err_h vm_store_open(uint32_t total_size, const uint16_t counts[VM_REG_CNT]) {
  SE_CHECK_NOT_NULL(counts);

  if (total_size == 0 || total_size > VM_STORE_MAX_POOL) {
    SE_RET_ERR(ERR_VM_LOAD_TOO_BIG, .requested = total_size, .available = VM_STORE_MAX_POOL);
  }

  uint32_t registry_bytes = 0;
  for (int r = 0; r < VM_REG_CNT; r++) registry_bytes += (uint32_t)counts[r] * sizeof(void*);
  if (registry_bytes > total_size) {
    SE_RET_ERR(ERR_VM_LOAD_TOO_BIG, .requested = registry_bytes, .available = total_size);
  }

  // Allocate before tearing down the old pool, so a failed load leaves the
  // running program intact.
  uint8_t* pool = (uint8_t*)heap_caps_malloc(total_size, VM_STORE_HEAP_CAPS);
  if (!pool) {
    // Largest *contiguous* block, not free total -- a fragmented heap can
    // have plenty free and still fail this.
    SE_RET_ERR(ERR_VM_LOAD_TOO_BIG, .requested = total_size, .available = (uint32_t)heap_caps_get_largest_free_block(VM_STORE_HEAP_CAPS));
  }

  vm_store_reset();  // frees the previous pool, now that the new one is secured
  s_pool = pool;
  arena_init(&g_vm_store.arena, pool, total_size);

  for (int r = 0; r < VM_REG_CNT; r++) {
    uint16_t n = counts[r];
    if (n == 0) continue;  // a program with none of this kind is legal, just inert

    void** items = (void**)arena_carve(&g_vm_store.arena, (uint32_t)n * sizeof(void*));
    if (!items) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
    memset(items, 0, (size_t)n * sizeof(void*));
    g_vm_store.reg[r].items = items;
    g_vm_store.reg[r].count = n;
  }
  return NULL;
}

err_h vm_store_alloc(void** out, vm_reg_e r, uint16_t id, uint32_t size) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  // id is untrusted (off the wire), so it's validated before any arena is
  // spent: a rejected program leaves its retry the space it needs.
  vm_registry_t* g = NULL;
  if (id != VM_ID_NONE) {
    if ((unsigned)r >= VM_REG_CNT) {
      SE_RET_ERR(ERR_VM_REG_OOB, .kind = (uint8_t)r, .id = id, .count = 0);
    }
    g = &g_vm_store.reg[r];
    if (id >= g->count || g->items == NULL) {
      SE_RET_ERR(ERR_VM_REG_OOB, .kind = (uint8_t)r, .id = id, .count = g->count);
    }
    if (g->items[id] != NULL) {  // each id is written exactly once per load
      SE_RET_ERR(ERR_VM_REG_DUP, .kind = (uint8_t)r, .id = id);
    }
  }

  void* p = arena_carve(&g_vm_store.arena, size);
  if (!p) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  memset(p, 0, size);

  if (g) g->items[id] = p;
  *out = p;
  return NULL;
}

uint32_t vm_store_used(void) {
  return g_vm_store.arena.offset;
}

uint32_t vm_store_capacity(void) {
  return g_vm_store.arena.capacity;
}
