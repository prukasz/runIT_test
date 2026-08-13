#include "vm_obj_build.h"
#include <string.h>

#define OWNER OWNER_VM_OBJ

err_h vm_obj_shape(const vm_obj_head_t* head, uint32_t* out_total) {
  SE_CHECK_NOT_NULL(head);
  SE_CHECK_NOT_NULL(out_total);
  *out_total = 0;

  vm_obj_t_e type = (vm_obj_t_e)head->d.obj_t;
  uint8_t w = vm_type_width(type);
  if (type == VM_OBJ_NONE || w == 0) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_TYPE, .type = (uint8_t)type);
  }

  /* An object with no storage still has an address: every resolve path would
     hand back a pointer to where its payload *would* start, which is the next
     allocation's first byte. A read there returns a neighbour's value and a
     write corrupts it, silently in both directions. */
  if (head->payload_size == 0) {
    SE_RET_ERR(ERR_VM_OBJ_EMPTY, .type = (uint8_t)type);
  }

  /* payload_size arrives in bytes and nothing here computed it, so a partial
     trailing element has to be rejected rather than assumed away. Five bytes of
     U32 would let vm_obj_elem_ptr() hand back element 1 -- offset 4 passes the
     `off < payload_size` bound -- whose last three bytes lie past the payload,
     in the tag or the next allocation. Widths are powers of two (stage A pins
     that), so the mask is exact. */
  if (head->payload_size & (uint16_t)(w - 1)) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_SIZE, .type = (uint8_t)type, .payload_size = head->payload_size, .width = w);
  }

  /* A retentive object's payload is written to NVS verbatim and read back on
     the next boot. Pointer payloads are addresses into this boot's arena, so
     restoring them would install dangling handles -- reject at creation
     rather than discovering it during a flush. */
  if (head->f.retentive && type == VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_OBJ_RETENTIVE_PTR, .type = (uint8_t)type);
  }

  /* name_size is 4 bits, so it cannot exceed VM_OBJ_NAME_MAX and needs no
     check of its own -- the field is the limit. */
  *out_total = (uint32_t)sizeof(vm_obj_head_t) + head->payload_size + head->d.name_size;
  return NULL;
}

void vm_obj_init(vm_obj_h o, const vm_obj_head_t* head, const char* name) {
  o->head = *head;

  // the three fields a caller does not get to choose -- see vm_obj_build.h
  o->head.f.upd = 0;
  o->head.f.tagged = head->d.name_size ? 1 : 0;
  o->head.f.dynamic = 0;  // arena; vm_obj_dyn_create() sets it after this

  if (head->d.name_size && name) memcpy(o->payload + head->payload_size, name, head->d.name_size);
}

err_h vm_obj_create(vm_obj_h* out, uint16_t id, const vm_obj_head_t* head, const char* name) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  uint32_t total = 0;
  SE_RET_IF_ERR(vm_obj_shape(head, &total));

  vm_obj_h o = NULL;
  // allocates, zeroes and binds the id in one step -- see vm_store.h
  SE_RET_IF_ERR(vm_store_alloc((void**)&o, VM_REG_OBJ, id, total));

  vm_obj_init(o, head, name);
  *out = o;
  return NULL;
}

/* =========================================================================
   Accessors
   ========================================================================= */

err_h vm_accessor_create(vm_accessor_t** out, uint16_t id, uint16_t root_obj_id, uint8_t idx_count) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  /* One slice for the accessor and its index array: they have identical
     lifetimes and are always walked together, so splitting them would only
     add an allocation and a pointer to get wrong. */
  uint32_t total = (uint32_t)sizeof(vm_accessor_t) + (uint32_t)idx_count * sizeof(vm_index_t);
  vm_accessor_t* acc = NULL;
  SE_RET_IF_ERR(vm_store_alloc((void**)&acc, VM_REG_ACC, id, total));

  acc->id = root_obj_id;
  acc->count = idx_count;
  acc->indices = idx_count ? (vm_index_t*)((uint8_t*)acc + sizeof(vm_accessor_t)) : NULL;

  *out = acc;
  return NULL;
}

// indices are const to readers; construction is the one place that writes them
static err_h index_slot(vm_accessor_t* acc, uint8_t pos, vm_index_t** out) {
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

  /* The wire form is unterminated and the frame buffer is gone once the
     packet is handled, so the name has to be copied into program storage. */
  char* copy = NULL;
  SE_RET_IF_ERR(vm_store_alloc((void**)&copy, VM_REG_ACC, VM_ID_NONE, (uint32_t)name_len + 1u));
  memcpy(copy, name, name_len);
  copy[name_len] = '\0';

  slot->kind = VM_IDX_NAME;
  slot->name_len = name_len;  // measured here so the scan never calls strlen()
  slot->name = copy;
  return NULL;
}

bool vm_accessor_cache_build(vm_accessor_t* acc) {
  if (!acc) return false;

  /* Cleared first: rebuilding an accessor whose object went away, or whose
     shape stopped qualifying, must drop the old entry rather than leave a
     stale address behind a set flag. */
  acc->flags &= (uint8_t)~VM_ACC_F_CACHED;

  if (acc->count > 1) return false;  // deeper chains cross a link -- see the header

  vm_obj_h obj = vm_obj_by_id(acc->id);
  if (!obj) return false;  // accessor built before its object; stays uncached

  if (acc->count == 0) {
    acc->c_payload = vm_obj_as_payload(obj);
  } else {
    if (acc->indices[0].kind != VM_IDX_LITERAL) return false;
    uint8_t* p = vm_obj_elem_ptr(obj, acc->indices[0].value);
    if (!p) return false;  // out of range -- leave it to report properly at access
    acc->c_payload = (vm_payload_t){.ptr = p, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
  }

  acc->c_owner = obj;
  acc->flags |= VM_ACC_F_CACHED;
  return true;
}
