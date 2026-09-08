#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_obj.h"
#include "vm_obj_access.h"

/*
Object construction -- the counterpart to vm_obj_access.h, which only ever
reads and writes objects that already exist.

Two callers, deliberately sharing one implementation: the program loader
building the static object graph from an uploaded packet, and any block that
has to build a shape at runtime. Both go through vm_store_alloc(), which
carves the chunk, zeroes it and binds the id in one step; neither ever frees
an individual object, since storage is reclaimed by resetting the whole
store (see vm_store.h).

Building a nested object is two steps, in this order:
  1. create the children,
  2. create the VM_OBJ_PTR parent and vm_obj_link_direct() each child in.
A parent's payload holds vm_obj_h values, so every child must already exist
and have a final address before the parent can point at it.
*/

/*
An object is described by the same vm_obj_head_t it will be stored with -- there
is no separate "config" type. One representation, so building an object, reading
one back and cloning the shape of an existing one are all the same operation on
the same struct.

`payload_size` is **bytes, and the caller's arithmetic**. The VM does not
multiply an element count by a type width anywhere, which means it also cannot
catch that multiply overflowing: whoever converts a count into bytes owns that
check. For an uploaded program that is vm_loader.c, which still raises
ERR_VM_OBJ_TOO_LARGE before it ever gets here. Nothing downstream is put at
risk by a wrong size, because vm_obj_elem_ptr() bounds every access against
payload_size -- an undersized object refuses reads past its end rather than
reaching into its neighbour.

Three head fields are the creator's, not the caller's, and are overwritten
whatever the caller left in them:

  upd        always starts clear
  tagged     derived from name_size
  dynamic    set by the allocator -- vm_obj_create() clears it, and
             vm_obj_dyn_create() sets it. A caller cannot ask for either.
*/

/**
 * @brief Bump-allocate and initialise one object from its head.
 *
 * Payload and name bytes are zeroed, so a freshly created object reads as 0 /
 * empty rather than whatever the arena last held.
 *
 * @param out Receives the new handle; set to NULL on any failure.
 * @param id Registry id to bind, or VM_ID_NONE to allocate without binding.
 * @param head Shape and flags. `payload_size` is bytes; `d.name_size` says how
 *             many bytes of @p name to copy.
 * @param name `head->d.name_size` bytes, not NUL-terminated and not read at all
 *             when that is 0.
 * @return err_h NULL on success. ERR_VM_OBJ_BAD_TYPE for a width-less type,
 *         ERR_VM_OBJ_EMPTY for payload_size 0 -- an object with no storage
 *         still has an address, and that address belongs to the next
 *         allocation -- ERR_VM_OBJ_RETENTIVE_PTR for a retentive pointer,
 *         ERR_VM_REG_OOB / ERR_VM_REG_DUP for the id, or ERR_BASE_NO_MEM when
 *         the arena is full, in which case vm_store has separately reported
 *         the requested and remaining byte counts.
 *
 * @code
 * vm_obj_h temp;
 * vm_obj_head_t h = {.payload_size = sizeof(float)};
 * h.d.obj_t = VM_OBJ_F;
 * h.d.name_size = 4;
 * h.f.mutable = 1;
 * SE_RET_IF_ERR(vm_obj_create(&temp, 3, &h, "temp"));
 * @endcode
 */
err_h vm_obj_create(vm_obj_h* out, uint16_t id, const vm_obj_head_t* head, const char* name);

/**
 * @brief Validate a head and report the bytes one object of that shape needs.
 *
 * Split out because there are two allocators -- the arena (vm_obj_create) and
 * the heap (vm_obj_dyn_create in vm_obj_dyn.h) -- and only the allocation
 * differs. Every rejection listed for vm_obj_create() is raised here, before
 * either of them spends anything.
 *
 * @param out_total Receives head + payload + name bytes; 0 on any failure.
 */
err_h vm_obj_shape(const vm_obj_head_t* head, uint32_t* out_total);

/**
 * @brief Copy a validated head into a freshly allocated object, plus its name.
 *
 * The other half of the same split. Assumes @p o is zeroed and at least
 * vm_obj_shape()'s byte count, which is only true if that call succeeded on
 * this same head -- there is no validation here. Forces the three creator-owned
 * flags described above.
 */
void vm_obj_init(vm_obj_h o, const vm_obj_head_t* head, const char* name);

/* ===========================================================================
   Accessor construction

   Same shape as the object side: create each accessor, bind it to an id, fill
   its indices by position. The indices pointer addresses trailing storage in
   the same allocation as the header, and expires with that arena allocation.

   `VM_IDX_REF` takes an already-built accessor rather than an id on purpose:
   requiring the target to exist before the referencing index is written makes
   reference cycles impossible to construct, which is stronger than
   VM_ACCESSOR_MAX_DEPTH catching them later at resolve time.
   =========================================================================== */

/**
 * @brief Allocate an accessor plus its index array.
 *
 * Indices start as `VM_IDX_LITERAL 0`; fill them with the setters below.
 * `idx_count == 0` is a whole-object accessor and allocates no index array.
 *
 * @return err_h ERR_VM_REG_OOB / ERR_VM_REG_DUP for the id, ERR_BASE_NO_MEM
 *         when the arena is full.
 */
err_h vm_accessor_create(vm_accessor_t** out, uint16_t id, uint16_t root_obj_id, uint8_t idx_count);

/** @brief Set index `pos` to a fixed position. */
err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value);

/** @brief Set index `pos` to read its position live from `ref`. */
err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref);

/**
 * @brief Set index `pos` to match a child tag.
 *
 * Copies `name` into program storage NUL-terminated -- the wire form is not
 * terminated and the frame buffer does not outlive the load, so the accessor
 * cannot simply point at it. The copy is allocated unbound (VM_ID_NONE): it is
 * reached through the index, never by id.
 *
 * @return err_h ERR_VM_OBJ_NAME_TOO_LONG past VM_OBJ_NAME_MAX, ERR_BASE_NO_MEM.
 */
err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, const char* name, uint8_t name_len);

/**
 * @brief Pre-resolve an accessor whose address cannot move, so every later
 *        access is four loads instead of a walk.
 *
 * Qualifies: a whole-object accessor, or one literal index on a root object
 * whose element is in range. Both are fixed for the life of the program --
 * see the VM_ACC_F_CACHED note in vm_obj_access.h for why a pointer slot
 * counts as fixed and a two-level chain does not.
 *
 * Call after the root object exists and its indices are set. The loader does
 * this itself at the end of vm_loader_add_accessor(); code building accessors
 * directly has to call it. Skipping it costs speed and nothing else.
 *
 * @return true if the accessor is now cached, false if its shape does not
 *         qualify or the root object is missing -- never an error, since
 *         resolving the long way is always available.
 */
bool vm_accessor_cache_build(vm_accessor_t* acc);
