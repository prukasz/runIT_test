#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_obj.h"
#include "vm_obj_access.h"

/*
 * VM Object & Accessor Construction
 *
 * Provides allocation, validation, and initialization for VM objects and
 * accessors within the VM store (arena), along with accessor cache generation.
 *
 * Logic Flow:
 *   1. Object Construction (vm_obj_create, vm_obj_shape, vm_obj_init)
 *   2. Accessor Construction & Caching (vm_accessor_create, vm_accessor_set_*, vm_accessor_cache_build)
 */

// ===========================================================================
// 1. Object Construction
// ===========================================================================

/**
 * @brief Bump-allocate and initialize one object from its header descriptor.
 *
 * Validates shape, allocates from VM arena (zero-initialized), binds registry ID,
 * and initializes header and optional name tag.
 *
 * @param[out] out  Receives the new object handle (set to NULL on failure).
 * @param[in]  id   Registry ID to bind, or VM_ID_NONE to allocate without binding.
 * @param[in]  head Shape and descriptor flags.
 * @param[in]  name Name tag bytes (length specified by head->d.name_size).
 * @return err_h NULL on success, or error handle:
 *         - ERR_VM_OBJ_BAD_TYPE: Invalid or widthless type.
 *         - ERR_VM_OBJ_EMPTY: payload_size is zero.
 *         - ERR_VM_OBJ_BAD_SIZE: payload_size not a multiple of element width.
 *         - ERR_VM_OBJ_RETENTIVE_PTR: Pointer types cannot be retentive.
 *         - ERR_VM_REG_OOB / ERR_VM_REG_DUP: Registry ID invalid or already taken.
 *         - ERR_VM_ALLOC_EXHAUSTED: Arena out of memory.
 *
 * @code
 * vm_obj_h temp;
 * vm_obj_head_t h = vm_make_obj_head(VM_OBJ_F, 1, VM_OBJ_F_MUTABLE, 4);
 * SE_RET_IF_ERR(vm_obj_create(&temp, 3, &h, "temp"));
 * @endcode
 */
err_h vm_obj_create(vm_obj_h* out, uint16_t id, const vm_obj_head_t* head, const char* name);

/**
 * @brief Validate an object header and compute total allocation bytes required.
 *
 * Validates type, non-zero payload size, element alignment, and retentive
 * constraints. Shared by both arena and dynamic heap allocators.
 *
 * @param[in]  head      Object header to validate.
 * @param[out] out_total Receives total bytes (sizeof(vm_obj_head_t) + payload + name).
 * @return err_h NULL on success, or validation error handle.
 */
err_h vm_obj_shape(const vm_obj_head_t* head, uint32_t* out_total);

/**
 * @brief Initialize a pre-allocated object handle with validated header and name.
 *
 * Copies header, forces `upd` to 0, sets `tagged` bit if name_size > 0,
 * clears `dynamic` flag (default arena), and copies name bytes after payload.
 *
 * @param[in,out] o    Allocated object handle.
 * @param[in]     head Validated header descriptor.
 * @param[in]     name Optional name tag bytes.
 */
void vm_obj_init(vm_obj_h o, const vm_obj_head_t* head, const char* name);

// ===========================================================================
// 2. Accessor Construction & Caching
// ===========================================================================

/**
 * @brief Allocate an accessor and its index array in the VM store.
 *
 * Contiguously allocates accessor header and index array.
 * If idx_count == 0, creates a whole-object accessor without an index array.
 *
 * @param[out] out         Receives the new accessor handle (NULL on failure).
 * @param[in]  id          Registry ID to bind, or VM_ID_NONE.
 * @param[in]  root_obj_id Target root object ID.
 * @param[in]  idx_count   Number of index slots to allocate.
 * @return err_h NULL on success, or error handle (ERR_VM_REG_*, ERR_VM_ALLOC_EXHAUSTED).
 */
err_h vm_accessor_create(vm_accessor_t** out, uint16_t id, uint16_t root_obj_id, uint8_t idx_count);

/**
 * @brief Set accessor index at position @p pos to a literal index.
 *
 * @param[in,out] acc   Accessor handle.
 * @param[in]     pos   Index slot position (0 .. count-1).
 * @param[in]     value Literal index value.
 * @return err_h NULL on success, or ERR_VM_ACC_INDEX_OOB if pos >= count.
 */
err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value);

/**
 * @brief Set accessor index at position @p pos to read from another accessor.
 *
 * @param[in,out] acc Accessor handle.
 * @param[in]     pos Index slot position (0 .. count-1).
 * @param[in]     ref Target accessor reference.
 * @return err_h NULL on success, or ERR_VM_ACC_INDEX_OOB if pos >= count.
 */
err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref);

/**
 * @brief Set accessor index at position @p pos to match a child tag name.
 *
 * Allocates and copies @p name into VM store memory as a NUL-terminated string.
 *
 * @param[in,out] acc      Accessor handle.
 * @param[in]     pos      Index slot position (0 .. count-1).
 * @param[in]     name     Child tag name.
 * @param[in]     name_len Length of name (must be <= VM_OBJ_NAME_MAX).
 * @return err_h NULL on success, ERR_VM_OBJ_NAME_TOO_LONG, or ERR_VM_ALLOC_EXHAUSTED.
 */
err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, const char* name, uint8_t name_len);

/**
 * @brief Pre-resolve an immutable accessor target into its resolution cache.
 *
 * Pre-resolves whole-object accessors or single literal indices on root objects.
 * Caches target payload and owner for 4-load fast path execution.
 *
 * @param[in,out] acc Accessor to cache.
 * @return true if successfully cached, false if dynamic, chained, or uncachable.
 */
bool vm_accessor_cache_build(vm_accessor_t* acc);
