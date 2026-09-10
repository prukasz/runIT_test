#include "vm_errors.h"
#include <string.h>

/* Cold-path error builders (explicit OWNER passed per subsystem). */

/* ========================================================================= */
/* Accessor Layer Errors                                                     */
/* ========================================================================= */

__attribute__((noinline)) err_h vm_err_depth(uint16_t id) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_DEPTH_EXCEEDED, .id = id);
}

__attribute__((noinline)) err_h vm_err_unknown_id(uint16_t id) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
}

__attribute__((noinline)) err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, vm_obj_h obj) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = id, .chain_pos = pos, .expected = VM_OBJ_PTR, .actual = actual, .obj = (void*)obj);
}

__attribute__((noinline)) err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, vm_obj_h obj) {
  // saturate the 16-bit payload field so a huge index still reads as
  // "past the end" rather than a wrapped small number
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_OOB, .id = id, .chain_pos = pos, .index = (uint16_t)(index > UINT16_MAX ? UINT16_MAX : index), .obj = (void*)obj);
}

__attribute__((noinline)) err_h vm_err_null_obj(uint16_t id, uint8_t pos, vm_obj_h parent) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_NULL_OBJ, .id = id, .chain_pos = pos, .parent_obj = (void*)parent);
}

__attribute__((noinline)) err_h vm_err_chain_not_mutable(uint16_t id, uint8_t pos, vm_obj_h obj) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_NOT_MUTABLE, .id = id, .chain_pos = pos, .obj = (void*)obj);
}

__attribute__((noinline)) err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos) {
  return SE_WRAP_ERR_OWNED(OWNER_VM_ACCESSOR, cause, ERR_VM_ACCESSOR_INDEX_FAILED, .id = id, .chain_pos = pos);
}

// built by hand, not SE_RET_ERR: the payload carries a copied string, and
// designated initialisers can't fill a char array
__attribute__((noinline)) err_h vm_err_name_not_found(uint16_t id, uint8_t pos, const char* name) {
  err_h e = SE_ERR_NEW_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_NAME_NOT_FOUND, .id = id, .chain_pos = pos);
  err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t* pl = (err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t*)e->payload;
  size_t n = strlen(name);
  if (n >= sizeof(pl->name)) n = sizeof(pl->name) - 1;
  memcpy(pl->name, name, n);
  pl->name[n] = '\0';
  return e;
}

__attribute__((noinline)) err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id) {
  SE_RET_ERR_OWNED(OWNER_VM_ACCESSOR, ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = id, .chain_pos = 0, .expected = VM_OBJ_NONE, .actual = actual, .obj = (void*)owner);
}

/* ========================================================================= */
/* Object Layer Errors                                                       */
/* ========================================================================= */

__attribute__((noinline)) err_h vm_obj_null_obj_err(void) {
  SE_RET_ERR_OWNED(OWNER_VM_OBJ, ERR_NULL_PTR, 0);
}

__attribute__((noinline)) err_h vm_obj_not_mutable_err(vm_obj_h obj) {
  SE_RET_ERR_OWNED(OWNER_VM_OBJ, ERR_VM_OBJ_NOT_MUTABLE, .obj = (void*)obj);
}

__attribute__((noinline)) err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index) {
  SE_RET_ERR_OWNED(OWNER_VM_OBJ, ERR_VM_OBJ_OOB, .index = index, .obj = (void*)obj);
}

__attribute__((noinline)) err_h vm_obj_not_ptr_err(vm_obj_h obj, uint8_t actual) {
  SE_RET_ERR_OWNED(OWNER_VM_OBJ, ERR_VM_OBJ_NOT_PTR, .actual = actual, .obj = (void*)obj);
}

/* ========================================================================= */
/* Block Layer Errors                                                        */
/* ========================================================================= */

__attribute__((noinline)) err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out) {
  SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_MISSING, .block_idx = block_idx, .pin_id = pin_id, .is_out = is_out ? 1 : 0);
}

__attribute__((noinline)) err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out) {
  SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_UNLINKED, .block_idx = block_idx, .pin_id = pin_id, .is_out = is_out ? 1 : 0);
}
