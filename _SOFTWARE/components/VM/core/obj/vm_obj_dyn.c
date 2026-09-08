#include "vm_obj_dyn.h"
#include <string.h>
#include "esp_heap_caps.h"

#define OWNER OWNER_VM_OBJ

/* Same caps as the program pool: internal DRAM only. A message tree is walked
   by accessors on every pass that reads it, so putting it behind the PSRAM
   cache would make access cost depend on what else is resident. */
#define VM_DYN_HEAP_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)

vm_obj_dyn_meta_t g_vm_dyn[VM_DYN_MAX] = {0};

typedef struct {
  vm_obj_h* cell;
  vm_obj_h child;
  uint8_t visiting[VM_DYN_MAX];
  uint8_t height[VM_DYN_MAX];
} ownership_check_t;

/* Validate all dynamic roots, including ancestors of the changed object.
   Checking only the inserted subtree misses a chain grown from its tail.
   Completed heights memoize shared subgraphs; visiting detects back edges. */
static err_h ownership_height(ownership_check_t* check, uint16_t id, uint8_t depth) {
  if (check->visiting[id]) {
    SE_RET_ERR(ERR_VM_OBJ_OWNERSHIP, .reason = VM_OWNERSHIP_CYCLE, .limit = VM_DYN_MAX_DEPTH);
  }
  if (depth >= VM_DYN_MAX_DEPTH) {
    SE_RET_ERR(ERR_VM_OBJ_OWNERSHIP, .reason = VM_OWNERSHIP_DEPTH, .limit = VM_DYN_MAX_DEPTH);
  }
  if (check->height[id]) return NULL;
  check->visiting[id] = 1;
  uint8_t height = 1;
  vm_obj_h o = g_vm_dyn[id].obj;
  if (o->head.d.obj_t == VM_OBJ_PTR) {
    vm_obj_h* kids = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < vm_obj_items_cnt(o); i++) {
      vm_obj_h kid = &kids[i] == check->cell ? check->child : kids[i];
      uint16_t kid_id = vm_obj_dyn_id(kid);
      if (kid_id == VM_DYN_NO_ID) continue;  // arena children have program lifetime
      SE_RET_IF_ERR(ownership_height(check, kid_id, (uint8_t)(depth + 1)));
      uint8_t next = (uint8_t)(check->height[kid_id] + 1);
      if (next > height) height = next;
    }
  }
  if (height > VM_DYN_MAX_DEPTH) {
    SE_RET_ERR(ERR_VM_OBJ_OWNERSHIP, .reason = VM_OWNERSHIP_DEPTH, .limit = VM_DYN_MAX_DEPTH);
  }
  check->visiting[id] = 0;
  check->height[id] = height;
  return NULL;
}

err_h vm_obj_dyn_check_link(vm_obj_h owner, vm_obj_h* cell, vm_obj_h child) {
  if (!owner->head.f.dynamic || *cell == child) return NULL;
  ownership_check_t check = {.cell = cell, .child = child};
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (g_vm_dyn[i].obj) SE_RET_IF_ERR(ownership_height(&check, i, 0));
  }
  return NULL;
}

err_h vm_obj_dyn_create(vm_obj_h* out, const vm_obj_head_t* head, const char* name) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  /* Shape is validated before a slot is taken or a byte allocated, so a
     rejected object costs neither -- the same rule the arena path follows. */
  uint32_t total = 0;
  SE_RET_IF_ERR(vm_obj_shape(head, &total));

  uint16_t slot = VM_DYN_MAX;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (!g_vm_dyn[i].obj) {
      slot = i;
      break;
    }
  }
  if (slot == VM_DYN_MAX) {
    SE_RET_ERR(ERR_VM_DYN_FULL, .limit = VM_DYN_MAX);
  }

  // zeroed for the same reason arena objects are: payload and name must read
  // as empty rather than as whatever the heap last held
  vm_obj_h o = (vm_obj_h)heap_caps_calloc(1, total, VM_DYN_HEAP_CAPS);
  if (!o) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  vm_obj_init(o, head, name);
  o->head.f.dynamic = 1;  // the one field the shared init cannot set

  g_vm_dyn[slot].obj = o;
  g_vm_dyn[slot].ref_cnt = 0;  // owned by nothing until something links it

  *out = o;
  return NULL;
}

void vm_obj_dyn_retain(vm_obj_h o) {
  // no-op on an arena object, so link paths need no type test of their own
  uint16_t id = vm_obj_dyn_id(o);
  if (id != VM_DYN_NO_ID) g_vm_dyn[id].ref_cnt++;
}

static void dyn_release(vm_obj_h o, uint8_t depth) {
  uint16_t id = vm_obj_dyn_id(o);
  if (id == VM_DYN_NO_ID) return;  // arena object or NULL; o must not be a freed handle

  if (g_vm_dyn[id].ref_cnt > 1) {
    g_vm_dyn[id].ref_cnt--;
    return;
  }

  // Legal ownership is a bounded DAG, validated before every link mutation.
  g_vm_dyn[id].obj = NULL;
  g_vm_dyn[id].ref_cnt = 0;

  /* Children go first -- reading the payload after free() would be a
     use-after-free. Arena children fall out of dyn_release() immediately, so a
     tree holding both kinds needs no test here. */
  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR && depth < VM_DYN_MAX_DEPTH) {
    vm_obj_h* kids = (vm_obj_h*)o->payload;
    uint16_t n = vm_obj_items_cnt(o);
    for (uint16_t i = 0; i < n; i++) {
      if (kids[i]) dyn_release(kids[i], (uint8_t)(depth + 1));
    }
  }

  heap_caps_free(o);
}

void vm_obj_dyn_release(vm_obj_h o) {
  dyn_release(o, 0);
}

void vm_obj_dyn_reset(void) {
  /* Reference counts are ignored and nothing recurses: every live object is in
     the register exactly once, so freeing each entry frees the lot. That is
     what makes a detached cycle, or an object allocated and never linked,
     unable to survive a reload -- neither is reachable from a parent, but both
     are in here. */
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (g_vm_dyn[i].obj) heap_caps_free(g_vm_dyn[i].obj);
    g_vm_dyn[i].obj = NULL;
    g_vm_dyn[i].ref_cnt = 0;
  }
}
