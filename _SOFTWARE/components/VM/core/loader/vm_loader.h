#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block_build.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_section.h"

/*
Program loading -- turns an uploaded description into the live object graph.

Every argument here comes off the wire, so this is the trust boundary: ids,
counts, sizes and offsets are all validated before they reach vm_obj_create()
or a registry. The wire parsing itself lives in the decoder
(dec_vm_loader.h); vm_store.h owns the storage, and this file owns the rules
and the load-order state machine.

Load sequence, which is also the teardown-safety story:

  vm_loader_reset()      -> registries detached, arena reset. Everything resolves
                            to NULL, so accessors and late callbacks fail
                            closed for the whole window before the next load.
  vm_loader_open(...)    -> reserve the id registries and cap the arena
  vm_loader_add_obj(...) -> once per object, any order of ids
  vm_loader_set_data(...) -> payload bytes, or child ids for VM_OBJ_PTR
                            parents; may be called repeatedly for slices

Objects must exist before anything links to them, so all add_obj packets
have to arrive before the set_data that references them as children. That is
the only ordering constraint between the two -- data for a plain scalar can
interleave freely.
*/

/**
 * @brief Wire class byte for program upload.
 *
 * Lives here rather than in the decoder because this class is 1:1 with this
 * component -- the same reason SYS_ACTIONS_CLASS_HEADER lives in
 * sys_actions.h. dec_vm_loader.h includes this header and uses it.
 */
#define VM_LOADER_CLASS_HEADER 0x04


typedef enum vm_load_state_e {
  VM_LOAD_EMPTY = 0,  // no storage; every id resolves to NULL
  VM_LOAD_OPEN = 1,   // storage reserved, objects may be added and filled
} vm_load_state_e;

/* Flag bits for loader objects are defined in vm_obj.h (VM_OBJ_F_* / VM_LOAD_F_*). */

/** @brief Control-task lifecycle boundary: wait for execution to quiesce,
 * clear runtime/subscriptions and both allocation domains, and leave stopped.
 * Never call from a block or a telemetry sample callback. */
void vm_loader_reset(void);

/**
 * @brief Reserve storage for a program.
 *
 * Quiesces execution, validates and reserves the new pool, then replaces the
 * program and clears subscriptions/runtime state. On failure the old storage,
 * loader state and run mode survive. On success execution remains stopped.
 *
 * @return err_h ERR_VM_LOAD_TOO_BIG if total_size exceeds the pool,
 *         ERR_BASE_NO_MEM if the id registries do not fit inside it.
 */
err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint16_t sec_cnt, uint32_t total_size);

/**
 * @brief Create one object and bind it to @p id.
 *
 * Shape and flags are described directly by @p head.
 *
 * @param id Registry slot to claim (0..obj_cnt-1).
 * @param head Descriptor specifying payload_size, obj_t, name_size, and flags.
 * @param name Tag bytes, not NUL-terminated on the wire; NULL when head->d.name_size is 0.
 * @return err_h the validation chain from vm_obj_create()
 *         -- unknown type, empty payload, bad payload alignment, retentive
 *         pointer, duplicate or out-of-range id.
 */
err_h vm_loader_add_obj(uint16_t id, const vm_obj_head_t* head, const char* name);

/**
 * @brief Fill part of an object's payload.
 * Load-time initialization only, with execution stopped. This can initialize
 * immutable/protected values; it is not the user runtime mutation API.
 *
 * For a VM_OBJ_PTR parent, @p data is a list of little-endian uint16 child
 * ids -- never raw pointers, which mean nothing off-device -- and each is
 * resolved through the registry and linked. For every other type @p data is raw
 * payload bytes.
 *
 * @param start_idx First element to write, in elements (not bytes).
 * @param len Length of @p data in bytes.
 * @return err_h ERR_VM_LOAD_DATA_RANGE if the write would run past the
 *         object, or the link error for an unresolvable child id.
 */
err_h vm_loader_set_data(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len);

/**
 * @brief Create one accessor and bind it to @p acc_id.
 *
 * Index records are walked from @p idx_data, each `{u8 kind, payload}`:
 * `VM_IDX_LITERAL` a u32 position, `VM_IDX_REF` a u16 accessor id,
 * `VM_IDX_NAME` a u8 length then that many unterminated bytes.
 *
 * A `VM_IDX_REF` target must already exist, which is what makes a reference
 * cycle impossible to build rather than merely caught at resolve time.
 *
 * @param idx_count Number of index records present in @p idx_data.
 * @param idx_len Length of @p idx_data in bytes; records are bounds-checked
 *                against it so a malformed count cannot overread.
 * @return err_h ERR_VM_ACC_BAD_KIND, ERR_VM_REG_OOB/_DUP,
 *         ERR_VM_LOAD_SHORT_RECORD, or the accessor build chain.
 */
err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len);

/**
 * @brief Create one block and bind it to @p blk_id.
 *
 * Last in the load order by necessity: a block names accessors (inputs, EN)
 * and objects (outputs, ENO) by id, and every one of them must already be
 * bound. Those ids are resolved to pointers here and the block keeps only the
 * pointers, so nothing downstream can hold an id that stopped being valid.
 *
 * The id is checked against the registry *before* the block is built, so a
 * duplicate or out-of-range id costs no arena -- a rejected program leaves
 * the space its retry needs.
 *
 * @return err_h ERR_VM_REG_OOB / ERR_VM_REG_DUP for the id,
 *         ERR_VM_BLK_BAD_SHAPE for an unbuildable pin count,
 *         ERR_VM_BLK_BAD_REF naming the slot whose id did not resolve, or
 *         ERR_BASE_NO_MEM.
 */
err_h vm_loader_add_block(uint16_t blk_id, const vm_block_cfg_t* cfg);

/**
 * @brief Declare one section: `[start, end)` over the block order.
 *
 * Last in the load order, after every block it covers -- a range is validated
 * against the block registry, so the blocks have to be there to validate it
 * against. Same rule blocks have with the accessors they name, one level up.
 *
 * A section is an atomic unit, not a schedule: every section runs every pass,
 * and what the range buys is a boundary the system may be interrupted at. A
 * program may legitimately declare none, in which case the whole order is one
 * uninterruptible section.
 *
 * @return err_h ERR_VM_SEC_BAD_RANGE, ERR_VM_SEC_OVERLAP, ERR_VM_REG_OOB /
 *         ERR_VM_REG_DUP for the id, or ERR_BASE_NO_MEM.
 */
err_h vm_loader_add_section(uint16_t sec_id, uint16_t start, uint16_t end);

/** @brief Current state -- decoders use it to reject out-of-sequence packets. */
vm_load_state_e vm_loader_state(void);

/** @brief Bytes consumed so far, for diagnostics after a load. */
uint32_t vm_loader_used(void);
