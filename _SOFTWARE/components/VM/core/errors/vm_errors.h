#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_obj.h"

/* Cold-path error builders for accessor, object, and block layers.
   Extracted as noinline to prevent stack frame inflation on hot paths. */

/* ========================================================================= */
/* Accessor Layer Errors                                                     */
/* ========================================================================= */

err_h vm_err_depth(uint16_t id);
err_h vm_err_unknown_id(uint16_t id);
err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, vm_obj_h obj);
err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, vm_obj_h obj);
err_h vm_err_null_obj(uint16_t id, uint8_t pos, vm_obj_h parent);
err_h vm_err_chain_not_mutable(uint16_t id, uint8_t pos, vm_obj_h obj);
err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos);
err_h vm_err_name_not_found(uint16_t id, uint8_t pos, const char* name);
err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id);

/* ========================================================================= */
/* Object Layer Errors                                                       */
/* ========================================================================= */

err_h vm_obj_null_obj_err(void);
err_h vm_obj_not_mutable_err(vm_obj_h obj);
err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index);
err_h vm_obj_not_ptr_err(vm_obj_h obj, uint8_t actual);

/* ========================================================================= */
/* Block Layer Errors                                                        */
/* ========================================================================= */

err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out);
err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out);
