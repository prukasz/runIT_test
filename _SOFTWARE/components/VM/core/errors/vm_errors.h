#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_obj.h"

/*
Every error the object, accessor and block layers raise from a *cold arm* --
one builder per shape, in one place.

Two things put them here rather than beside the code that raises them.

The first is why they were extracted at all, and it predates this file:
resolve_d() had eight inlined SE_ERR_NEW sites, which gave the walk a 160-byte
stack frame -- multiplied by VM_ACCESSOR_MAX_DEPTH on a by-ref chain -- for
arms that never run on a working program. The same is true of the inline write
path in vm_obj_access.h, which expands at every VM_OBJ_SET_VAL site.

The second is why they are together: SE_ERR_NEW records no file or line, so a
trace is identified by tag + owner + payload and nothing else. That makes the
error vocabulary a thing worth reading in one sitting -- which shapes exist,
which fields each one carries, and which of them are honest about what they
know. Scattering them across the layer hid, for a while, three builders that
filled an accessor id with 0 when no accessor was involved.

Owners are named per function rather than by an ambient `#define OWNER`,
because the accessor, object and block layers all raise errors from here and
each has to keep its own owner tag. See SE_RET_ERR_OWNED in sys_error_vm.h.
*/

// ---------------------------------------------------------------------------
// Accessor layer -- a chain was walked, so `id` and `chain_pos` mean something
// ---------------------------------------------------------------------------

err_h vm_err_depth(uint16_t id);
err_h vm_err_unknown_id(uint16_t id);

/** @brief Serves both the by-name parent check and the mid-chain step: both expect PTR. */
err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, vm_obj_h obj);

/** @brief `index` is 32-bit and saturates into the payload's 16, so a wild
 *  by-ref index still reads as "past the end" rather than a wrapped small one. */
err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, vm_obj_h obj);

err_h vm_err_null_obj(uint16_t id, uint8_t pos, vm_obj_h parent);
err_h vm_err_chain_not_mutable(uint16_t id, uint8_t pos, vm_obj_h obj);
err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos);

/** @brief Routine for variable-shape messages, not necessarily a bug. */
err_h vm_err_name_not_found(uint16_t id, uint8_t pos, const char* name);

/** @brief The scalar-store arm, which is the one place an accessor id is
 *  optional: VM_OBJ_SET_VAL has one, VM_OBJ_SET_VAL_AT passes VM_ID_NONE. */
err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id);

// ---------------------------------------------------------------------------
// Object layer -- handed a handle, with no chain to name. These deliberately
// carry no `id`/`chain_pos`: a field that can only ever hold "not applicable"
// is worse than absent, and 0 is a real accessor id rather than a sentinel.
// ---------------------------------------------------------------------------

err_h vm_obj_null_obj_err(void);
err_h vm_obj_not_mutable_err(vm_obj_h obj);
err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index);
err_h vm_obj_not_ptr_err(vm_obj_h obj, uint8_t actual);

// ---------------------------------------------------------------------------
// Block layer
// ---------------------------------------------------------------------------

err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out);
err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out);
