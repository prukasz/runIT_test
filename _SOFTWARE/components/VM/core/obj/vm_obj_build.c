#include "vm_obj_build.h"
#include <string.h>

#define OWNER OWNER_VM_OBJ

/*
 * VM Object & Accessor Construction Engine
 *
 * Logic Flow:
 *   1. Object Construction:
 *      - Shape validation & byte sizing (vm_obj_shape)
 *      - Object header & name initialization (vm_obj_init)
 *      - Object bump-allocation & registry bind (vm_obj_create)
 *   2. Accessor Construction & Caching:
 *      - Accessor allocation & registry bind (vm_accessor_create)
 *      - Index slot helper (index_slot)
 *      - Accessor index setters (vm_accessor_set_literal, vm_accessor_set_ref, vm_accessor_set_name)
 *      - Accessor cache pre-resolution (vm_accessor_cache_build)
 */

// ===========================================================================
// 1. Object Construction
// ===========================================================================

err_h vm_obj_shape(const vm_obj_head_t* head, uint32_t* out_total) {
  SE_CHECK_NOT_NULL(head);
  SE_CHECK_NOT_NULL(out_total);
  *out_total = 0;

  vm_obj_t_e type = (vm_obj_t_e)head->d.obj_t;
  uint8_t w = vm_type_width(type);
  if (type == VM_OBJ_NONE || w == 0) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_TYPE, .type = (uint8_t)type);
  }

  // Object must have non-zero payload storage
  if (head->payload_size == 0) {
    SE_RET_ERR(ERR_VM_OBJ_EMPTY, .type = (uint8_t)type);
  }

  // Payload size must align to element width (powers of two)
  if (head->payload_size & (uint16_t)(w - 1)) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_SIZE, .type = (uint8_t)type, .payload_size = head->payload_size, .width = w);
  }

  // Pointer objects cannot be retentive (pointers would dangle across reboots)
  if (head->f.retentive && type == VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_OBJ_RETENTIVE_PTR, .type = (uint8_t)type);
  }

  // Note: head->d.name_size is 4 bits, bounded to VM_OBJ_NAME_MAX (15)
  *out_total = (uint32_t)sizeof(vm_obj_head_t) + head->payload_size + head->d.name_size;
  return NULL;
}

void vm_obj_init(vm_obj_h o, const vm_obj_head_t* head, const char* name) {
  o->head = *head;
  o->head.f.upd = 0;
  o->head.f.tagged = head->d.name_size ? 1 : 0;
  o->head.f.dynamic = 0;

  if (head->d.name_size && name) {
    memcpy(o->payload + head->payload_size, name, head->d.name_size);
  }
}

err_h vm_obj_create(vm_obj_h* out, uint16_t id, const vm_obj_head_t* head, const char* name) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  uint32_t total = 0;
  SE_RET_IF_ERR(vm_obj_shape(head, &total));

  vm_obj_h o = NULL;
  SE_RET_IF_ERR(vm_store_alloc((void**)&o, VM_REG_OBJ, id, total));

  vm_obj_init(o, head, name);
  *out = o;
  return NULL;
}

// ===========================================================================
// 2. Accessor Construction & Caching
// ===========================================================================

err_h vm_accessor_create(vm_accessor_t** out, uint16_t id, uint16_t root_obj_id, uint8_t idx_count) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  // Single allocation holds accessor struct and its contiguous index array
  uint32_t total = (uint32_t)sizeof(vm_accessor_t) + (uint32_t)idx_count * sizeof(vm_index_t);
  vm_accessor_t* acc = NULL;
  SE_RET_IF_ERR(vm_store_alloc((void**)&acc, VM_REG_ACC, id, total));

  acc->id = root_obj_id;
  acc->count = idx_count;
  acc->indices = idx_count ? (vm_index_t*)((uint8_t*)acc + sizeof(vm_accessor_t)) : NULL;

  *out = acc;
  return NULL;
}

static inline err_h index_slot(vm_accessor_t* acc, uint8_t pos, vm_index_t** out) {
  SE_CHECK_NOT_NULL(acc);
  if (pos >= acc->count || acc->indices == NULL) {
    SE_RET_ERR(ERR_VM_ACC_INDEX_OOB, .acc_id = acc->id, .pos = pos, .count = acc->count);
  }
  *out = (vm_index_t*)&acc->indices[pos];
  return NULL;
}

err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value) {
  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_LITERAL;
  slot->value = value;
  return NULL;
}

err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref) {
  SE_CHECK_NOT_NULL(ref);
  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_REF;
  slot->ref = ref;
  return NULL;
}

err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, const char* name, uint8_t name_len) {
  SE_CHECK_NOT_NULL(name);
  if (name_len > VM_OBJ_NAME_MAX) {
    SE_RET_ERR(ERR_VM_OBJ_NAME_TOO_LONG, .len = name_len);
  }

  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));

  // Name bytes are copied into store NUL-terminated for permanent index matching
  char* copy = NULL;
  SE_RET_IF_ERR(vm_store_alloc((void**)&copy, VM_REG_ACC, VM_ID_NONE, (uint32_t)name_len + 1u));
  memcpy(copy, name, name_len);
  copy[name_len] = '\0';

  slot->kind = VM_IDX_NAME;
  slot->name_len = name_len;
  slot->name = copy;
  return NULL;
}

bool vm_accessor_cache_build(vm_accessor_t* acc) {
  if (!acc) return false;

  acc->flags &= (uint8_t)~VM_ACC_F_CACHED;

  // Multi-level chains cannot be cached: child pointers (a.b.c) can be dynamically repointed/cloned at runtime
  if (acc->count > 1) return false;

  // Root object must already exist in registry
  vm_obj_h obj = vm_obj_get_by_id(acc->id);
  if (!obj) return false;

  if (acc->count == 0) {
    // Whole-object direct address: payload starts at fixed offset 0
    acc->c_payload = vm_make_payload(obj);
  } else {
    // Dynamic ref (VM_IDX_REF) or tag scan (VM_IDX_NAME) cannot be cached: index changes or requires search
    if (acc->indices[0].kind != VM_IDX_LITERAL) return false;
    // Declared literal index must be within object capacity
    uint8_t* p = vm_obj_get_elem_ptr(obj, acc->indices[0].value);
    if (!p) return false;
    acc->c_payload = (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
  }

  acc->c_owner = obj;
  acc->flags |= VM_ACC_F_CACHED;
  return true;
}
