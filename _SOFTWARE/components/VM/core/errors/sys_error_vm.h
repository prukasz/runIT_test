#pragma once
#include <stdint.h>
#include <stdio.h>

// Owners for the VM component (block-scripting runtime: program storage,
// object arena, accessor resolution, block execution, code loading).
#define SYS_VM_OWNER_MAP(X)                        \
  X(OWNER_VM_BASE, 0xA900, "OWNER_VM_BASE")         \
  X(OWNER_VM_STORE, 0xA901, "OWNER_VM_STORE")       \
  X(OWNER_VM_OBJ, 0xA902, "OWNER_VM_OBJ")           \
  X(OWNER_VM_ACCESSOR, 0xA903, "OWNER_VM_ACCESSOR") \
  X(OWNER_VM_BLOCK, 0xA904, "OWNER_VM_BLOCK")       \
  X(OWNER_VM_CODE, 0xA905, "OWNER_VM_CODE")         \
  X(OWNER_VM_LOADER, 0xA906, "OWNER_VM_LOADER")     \
  X(OWNER_DEC_VM_LOADER, 0xA907, "OWNER_DEC_VM_LOADER") \
  X(OWNER_VM_EXEC, 0xA908, "OWNER_VM_EXEC")

// `obj`/`parent_obj` carry the failing vm_obj_t* for local (serial) trace
// diagnosis -- meaningless once encoded to a remote client with no symbol
// table, so a BLE-side encoder for these should drop the field, not send it.
#define SYS_ERROR_VM_MAP(X)                                                                                          \
  X(ERR_VM_ALLOC_EXHAUSTED, struct { uint32_t requested; uint32_t remaining; })                                       \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID, struct { uint16_t id; })                                                              \
  X(ERR_VM_ACCESSOR_OOB, struct { uint16_t id; uint8_t chain_pos; uint16_t index; void* obj; })                       \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH, struct { uint16_t id; uint8_t chain_pos; uint8_t expected; uint8_t actual; void* obj; }) \
  X(ERR_VM_ACCESSOR_NULL_OBJ, struct { uint16_t id; uint8_t chain_pos; void* parent_obj; })                           \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, struct { uint16_t id; })                                                          \
  X(ERR_VM_ACCESSOR_INDEX_FAILED, struct { uint16_t id; uint8_t chain_pos; })                                         \
  X(ERR_VM_ACCESSOR_NAME_NOT_FOUND, struct { uint16_t id; uint8_t chain_pos; char name[16]; })                        \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE, struct { uint16_t id; uint8_t chain_pos; void* obj; })                               \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED, struct { uint16_t block_idx; uint8_t input_idx; })                                 \
  X(ERR_VM_BLOCK_PIN_MISSING, struct { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                         \
  X(ERR_VM_BLOCK_PIN_UNLINKED, struct { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                        \
  X(ERR_VM_BLOCK_FAILED, struct { uint16_t block_idx; uint8_t block_type; })                                          \
  X(ERR_VM_OBJ_COPY_MISMATCH, struct { uint8_t src_type; uint8_t dst_type; uint16_t src_size; uint16_t dst_size; })   \
  X(ERR_VM_OBJ_COPY_SHAPE, struct { uint16_t index; uint8_t depth; uint8_t reason; })                                 \
  X(ERR_VM_OBJ_NOT_MUTABLE, struct { void* obj; })                                                                    \
  X(ERR_VM_OBJ_OOB, struct { uint16_t index; void* obj; })                                                            \
  X(ERR_VM_OBJ_NOT_PTR, struct { uint8_t actual; void* obj; })                                                        \
  X(ERR_VM_OBJ_BAD_TYPE, struct { uint8_t type; })                                                                    \
  X(ERR_VM_OBJ_EMPTY, struct { uint8_t type; })                                                                       \
  X(ERR_VM_OBJ_BAD_SIZE, struct { uint8_t type; uint16_t payload_size; uint8_t width; })                              \
  X(ERR_VM_OBJ_NAME_TOO_LONG, struct { uint8_t len; })                                                                \
  X(ERR_VM_OBJ_RETENTIVE_PTR, struct { uint8_t type; })                                                               \
  X(ERR_VM_LOAD_BAD_STATE, struct { uint8_t state; uint8_t expected; })                                               \
  X(ERR_VM_LOAD_TOO_BIG, struct { uint32_t requested; uint32_t available; })                                          \
  X(ERR_VM_LOAD_DATA_RANGE, struct { uint16_t id; uint16_t start_idx; uint16_t len; uint16_t items; })                \
  X(ERR_VM_LOAD_SHORT_RECORD, struct { uint8_t packet; uint16_t need; uint16_t got; })                                \
  X(ERR_VM_ACC_INDEX_OOB, struct { uint16_t acc_id; uint8_t pos; uint8_t count; })                                    \
  X(ERR_VM_ACC_BAD_KIND, struct { uint16_t acc_id; uint8_t pos; uint8_t kind; })                                      \
  X(ERR_VM_REG_OOB, struct { uint8_t kind; uint16_t id; uint16_t count; })                                            \
  X(ERR_VM_REG_DUP, struct { uint8_t kind; uint16_t id; })                                                            \
  X(ERR_VM_BLK_BAD_SHAPE, struct { uint16_t blk_id; uint8_t in_cnt; uint8_t q_cnt; })                                 \
  X(ERR_VM_BLK_BAD_REF, struct { uint16_t blk_id; uint16_t ref_id; uint8_t slot; uint8_t kind; })                      \
  X(ERR_VM_DYN_FULL, struct { uint16_t limit; })                                                                      \
  X(ERR_VM_BLK_UNKNOWN_TYPE, struct { uint16_t blk_id; uint8_t block_type; })                                         \
  X(ERR_VM_SEC_BAD_RANGE, struct { uint16_t sec_id; uint16_t start; uint16_t end; uint16_t blk_cnt; })                \
  X(ERR_VM_SEC_OVERLAP, struct { uint16_t sec_id; uint16_t other_id; uint16_t start; uint16_t end; })                 \
  X(ERR_VM_EXEC_BLOCK_HUNG, struct { uint16_t block_idx; uint16_t ms; })                                              \
  X(ERR_VM_EXEC_SPAN_DEPTH, struct { uint16_t block_idx; uint8_t depth; })                                            \
  X(ERR_VM_EXEC_BAD_SPAN, struct { uint16_t block_idx; uint16_t start; uint16_t end; })                               \
  X(ERR_VM_EVENT_OVERFLOW, struct { uint16_t type; uint16_t depth; uint16_t dropped; })                               \
  X(ERR_VM_EXPR_BAD_CODE, struct { uint16_t block_idx; uint16_t pc; uint8_t opcode; uint8_t reason; })                \
  X(ERR_VM_EXPR_MATH, struct { uint16_t block_idx; uint16_t pc; uint8_t opcode; uint8_t reason; })                    \
  X(ERR_VM_FOR_BAD_LOOP, struct { uint16_t block_idx; uint32_t turns; uint16_t cap; uint8_t reason; }) \
  X(ERR_VM_OBJ_OWNERSHIP, struct { uint8_t reason; uint8_t limit; }) \
  X(ERR_VM_OBJ_USR_PROTECTED, struct { void* obj; }) \
  X(ERR_VM_LOAD_RUNNING, struct { uint8_t mode; })

/**
 * @brief Human-readable descriptions for the VM tags - see
 * SE_describe_payload() in sys_error.h. Same two-step split as every other
 * module: this file only supplies LOG_BODY_<tag> text macros, sys_error.h
 * stamps them into typed logger functions once err_payload_<tag>_t exists.
 */
#define SYS_ERROR_VM_LOGGER_MAP(X) \
  X(ERR_VM_ALLOC_EXHAUSTED)        \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID)    \
  X(ERR_VM_ACCESSOR_OOB)           \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH) \
  X(ERR_VM_ACCESSOR_NULL_OBJ)      \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED) \
  X(ERR_VM_ACCESSOR_INDEX_FAILED)  \
  X(ERR_VM_ACCESSOR_NAME_NOT_FOUND) \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE)   \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED) \
  X(ERR_VM_BLOCK_PIN_MISSING)      \
  X(ERR_VM_BLOCK_PIN_UNLINKED)     \
  X(ERR_VM_BLOCK_FAILED)           \
  X(ERR_VM_OBJ_COPY_MISMATCH)      \
  X(ERR_VM_OBJ_COPY_SHAPE)         \
  X(ERR_VM_OBJ_NOT_MUTABLE)        \
  X(ERR_VM_OBJ_OOB)                \
  X(ERR_VM_OBJ_NOT_PTR)            \
  X(ERR_VM_OBJ_BAD_TYPE)           \
  X(ERR_VM_OBJ_EMPTY)              \
  X(ERR_VM_OBJ_BAD_SIZE)           \
  X(ERR_VM_OBJ_NAME_TOO_LONG)      \
  X(ERR_VM_OBJ_RETENTIVE_PTR)      \
  X(ERR_VM_LOAD_BAD_STATE)         \
  X(ERR_VM_LOAD_TOO_BIG)           \
  X(ERR_VM_LOAD_DATA_RANGE)        \
  X(ERR_VM_LOAD_SHORT_RECORD)      \
  X(ERR_VM_ACC_INDEX_OOB)          \
  X(ERR_VM_ACC_BAD_KIND)           \
  X(ERR_VM_REG_OOB)                \
  X(ERR_VM_REG_DUP)                \
  X(ERR_VM_BLK_BAD_SHAPE)          \
  X(ERR_VM_BLK_BAD_REF)            \
  X(ERR_VM_DYN_FULL)               \
  X(ERR_VM_BLK_UNKNOWN_TYPE)       \
  X(ERR_VM_SEC_BAD_RANGE)          \
  X(ERR_VM_SEC_OVERLAP)            \
  X(ERR_VM_EXEC_BLOCK_HUNG)        \
  X(ERR_VM_EXEC_SPAN_DEPTH)        \
  X(ERR_VM_EXEC_BAD_SPAN)          \
  X(ERR_VM_EVENT_OVERFLOW)         \
  X(ERR_VM_EXPR_BAD_CODE)          \
  X(ERR_VM_EXPR_MATH)              \
  X(ERR_VM_FOR_BAD_LOOP) \
  X(ERR_VM_OBJ_OWNERSHIP) \
  X(ERR_VM_OBJ_USR_PROTECTED) \
  X(ERR_VM_LOAD_RUNNING)

#define LOG_BODY_ERR_VM_LOAD_RUNNING(p, out, out_size) \
  snprintf((out), (out_size), "program initialization requires stopped execution (mode %u)", (p)->mode)

#define LOG_BODY_ERR_VM_OBJ_OWNERSHIP(p, out, out_size) \
  snprintf((out), (out_size), "dynamic ownership: %s (depth limit %u)", (p)->reason == 0 ? "cycle" : "too deep", (p)->limit)
#define LOG_BODY_ERR_VM_OBJ_USR_PROTECTED(p, out, out_size) \
  snprintf((out), (out_size), "object is protected from user writes (obj=%p)", (p)->obj)

#define LOG_BODY_ERR_VM_ALLOC_EXHAUSTED(p, out, out_size) \
  snprintf((out), (out_size), "vm arena exhausted: requested %lu, %lu remaining", (unsigned long)(p)->requested, (unsigned long)(p)->remaining)
#define LOG_BODY_ERR_VM_ACCESSOR_UNKNOWN_ID(p, out, out_size) \
  snprintf((out), (out_size), "accessor referenced unknown object id %u", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_OOB(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: index %u out of range at chain position %u (obj=%p)", (p)->id, (p)->index, (p)->chain_pos, (p)->obj)
#define LOG_BODY_ERR_VM_ACCESSOR_TYPE_MISMATCH(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: expected type %u at chain position %u, got %u (obj=%p)", (p)->id, (p)->expected, (p)->chain_pos, (p)->actual, (p)->obj)
#define LOG_BODY_ERR_VM_ACCESSOR_NULL_OBJ(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: chain position %u dereferenced a null object (parent=%p)", (p)->id, (p)->chain_pos, (p)->parent_obj)
#define LOG_BODY_ERR_VM_ACCESSOR_DEPTH_EXCEEDED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u exceeded max nesting depth", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_INDEX_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: dynamic index at chain position %u failed to resolve", (p)->id, (p)->chain_pos)
#define LOG_BODY_ERR_VM_ACCESSOR_NAME_NOT_FOUND(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: no child tagged '%s' at chain position %u", (p)->id, (p)->name, (p)->chain_pos)
#define LOG_BODY_ERR_VM_ACCESSOR_NOT_MUTABLE(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: write rejected at chain position %u, target is not mutable (obj=%p)", (p)->id, (p)->chain_pos, (p)->obj)
#define LOG_BODY_ERR_VM_BLOCK_INPUT_UNRESOLVED(p, out, out_size) \
  snprintf((out), (out_size), "block %u input %u failed to resolve", (p)->block_idx, (p)->input_idx)
#define LOG_BODY_ERR_VM_BLOCK_PIN_MISSING(p, out, out_size) \
  snprintf((out), (out_size), "block %u has no %s pin %u", (p)->block_idx, (p)->is_out ? "output" : "input", (p)->pin_id)
#define LOG_BODY_ERR_VM_BLOCK_PIN_UNLINKED(p, out, out_size) \
  snprintf((out), (out_size), "block %u %s pin %u is not linked", (p)->block_idx, (p)->is_out ? "output" : "input", (p)->pin_id)
#define LOG_BODY_ERR_VM_BLOCK_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "block %u (type %u) failed", (p)->block_idx, (p)->block_type)
#define LOG_BODY_ERR_VM_OBJ_COPY_MISMATCH(p, out, out_size)                                                  \
  snprintf((out), (out_size), "object copy mismatch: src type %u size %u, dst type %u size %u", (p)->src_type, \
           (p)->src_size, (p)->dst_type, (p)->dst_size)
/* The two trees disagreed about their *structure* rather than about a value:
   COPY_MISMATCH already says "these two payloads do not match", so this one
   only has to say where the walk gave up. `reason` is a VM_COPY_SHAPE_* code
   from vm_obj_access.h. */
#define VM_COPY_SHAPE_NAME(r) ((r) == 0 ? "tree deeper than the cap, or a cycle" : (r) == 1 ? "source slot empty, target holds an object" : "target slot empty, source holds an object")
#define LOG_BODY_ERR_VM_OBJ_COPY_SHAPE(p, out, out_size)                                                    \
  snprintf((out), (out_size), "object copy: %s at depth %u, slot %u", VM_COPY_SHAPE_NAME((p)->reason),      \
           (p)->depth, (p)->index)
/* The object-level halves of three accessor errors. An accessor id and a chain
   position are only meaningful when a chain was walked, and the direct entry
   points -- vm_obj_set_scalar_direct(), vm_obj_link_direct(), and the walk
   inside vm_obj_copy_content() once it is below where the chain ended -- were
   handed a raw handle instead. They used to fill both fields with 0, which is
   not a sentinel: VM_ID_NONE is 0xFFFF, so every one of those traces named
   accessor 0, an innocent bystander. */
#define LOG_BODY_ERR_VM_OBJ_NOT_MUTABLE(p, out, out_size) \
  snprintf((out), (out_size), "object is not mutable (obj=%p)", (p)->obj)
#define LOG_BODY_ERR_VM_OBJ_OOB(p, out, out_size) \
  snprintf((out), (out_size), "object index %u is past the end of its payload (obj=%p)", (p)->index, (p)->obj)
#define LOG_BODY_ERR_VM_OBJ_NOT_PTR(p, out, out_size) \
  snprintf((out), (out_size), "object holds type %u; a link needs VM_OBJ_PTR (obj=%p)", (p)->actual, (p)->obj)
#define LOG_BODY_ERR_VM_OBJ_BAD_TYPE(p, out, out_size) \
  snprintf((out), (out_size), "object create: type %u has no defined width", (p)->type)
#define LOG_BODY_ERR_VM_OBJ_EMPTY(p, out, out_size) \
  snprintf((out), (out_size), "object create: type %u with zero items has no storage", (p)->type)
#define LOG_BODY_ERR_VM_OBJ_BAD_SIZE(p, out, out_size)                                                       \
  snprintf((out), (out_size), "object create: payload of %u bytes is not a whole number of %u-byte type %u", \
           (p)->payload_size, (p)->width, (p)->type)
#define LOG_BODY_ERR_VM_OBJ_NAME_TOO_LONG(p, out, out_size) \
  snprintf((out), (out_size), "object create: name is %u chars, max is 15", (p)->len)
#define LOG_BODY_ERR_VM_OBJ_RETENTIVE_PTR(p, out, out_size) \
  snprintf((out), (out_size), "object create: retentive is meaningless for pointer type %u", (p)->type)
#define LOG_BODY_ERR_VM_DYN_FULL(p, out, out_size) \
  snprintf((out), (out_size), "dynamic object register is full: %u live, none released", (p)->limit)
#define LOG_BODY_ERR_VM_LOAD_BAD_STATE(p, out, out_size) \
  snprintf((out), (out_size), "loader in state %u, this packet needs state %u", (p)->state, (p)->expected)
#define LOG_BODY_ERR_VM_LOAD_TOO_BIG(p, out, out_size)                                            \
  snprintf((out), (out_size), "program needs %lu bytes, VM pool holds %lu", (unsigned long)(p)->requested, \
           (unsigned long)(p)->available)
#define LOG_BODY_ERR_VM_LOAD_DATA_RANGE(p, out, out_size)                                             \
  snprintf((out), (out_size), "object %u: write of %u elements at %u exceeds its %u items", (p)->id, (p)->len, \
           (p)->start_idx, (p)->items)
#define LOG_BODY_ERR_VM_LOAD_SHORT_RECORD(p, out, out_size) \
  snprintf((out), (out_size), "packet 0x%02X truncated: need %u bytes, got %u", (p)->packet, (p)->need, (p)->got)
#define LOG_BODY_ERR_VM_ACC_INDEX_OOB(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: index position %u past its %u declared indices", (p)->acc_id, (p)->pos, (p)->count)
#define LOG_BODY_ERR_VM_ACC_BAD_KIND(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u index %u: unknown kind %u", (p)->acc_id, (p)->pos, (p)->kind)
/* One tag for all three registries -- `kind` is a vm_reg_e, named here so a
   trace still reads as "object"/"accessor"/"block" rather than as a number. */
#define VM_REG_NAME(k) ((k) == 0 ? "object" : (k) == 1 ? "accessor" : (k) == 2 ? "block" : (k) == 3 ? "section" : "?")
#define LOG_BODY_ERR_VM_REG_OOB(p, out, out_size)   snprintf((out), (out_size), "%s registry: id %u is outside the %u-entry table", VM_REG_NAME((p)->kind), (p)->id, (p)->count)
#define LOG_BODY_ERR_VM_REG_DUP(p, out, out_size)   snprintf((out), (out_size), "%s registry: id %u is already bound", VM_REG_NAME((p)->kind), (p)->id)
#define LOG_BODY_ERR_VM_BLK_BAD_SHAPE(p, out, out_size) \
  snprintf((out), (out_size), "block %u: %u inputs / %u outputs is not a buildable shape", (p)->blk_id, (p)->in_cnt, (p)->q_cnt)
/* slot is the pin index; kind says which table the id failed to resolve in,
   so one tag covers all four wiring points instead of four near-identical ones */
#define LOG_BODY_ERR_VM_BLK_BAD_REF(p, out, out_size)                                                                                                                        \
  snprintf((out), (out_size), "block %u %s%u: id %u does not resolve", (p)->blk_id,                                                                                          \
           (p)->kind == 0   ? "input "                                                                                                                                       \
           : (p)->kind == 1 ? "output "                                                                                                                                      \
           : (p)->kind == 2 ? "EN"                                                                                                                                           \
                            : "ENO",                                                                                                                                         \
           (p)->slot, (p)->ref_id)

#define LOG_BODY_ERR_VM_BLK_UNKNOWN_TYPE(p, out, out_size) \
  snprintf((out), (out_size), "block %u: nothing in the palette runs type %u", (p)->blk_id, (p)->block_type)
#define LOG_BODY_ERR_VM_SEC_BAD_RANGE(p, out, out_size)                                                  \
  snprintf((out), (out_size), "section %u: range [%u,%u) is not inside the %u-block order", (p)->sec_id,   \
           (p)->start, (p)->end, (p)->blk_cnt)
#define LOG_BODY_ERR_VM_SEC_OVERLAP(p, out, out_size)                                                       \
  snprintf((out), (out_size), "section %u: range [%u,%u) overlaps section %u", (p)->sec_id, (p)->start,     \
           (p)->end, (p)->other_id)
#define LOG_BODY_ERR_VM_EXEC_BLOCK_HUNG(p, out, out_size) \
  snprintf((out), (out_size), "block %u has been executing for over %u ms", (p)->block_idx, (p)->ms)
#define LOG_BODY_ERR_VM_EXEC_SPAN_DEPTH(p, out, out_size) \
  snprintf((out), (out_size), "block %u: span nesting exceeded depth %u", (p)->block_idx, (p)->depth)
#define LOG_BODY_ERR_VM_EXEC_BAD_SPAN(p, out, out_size) \
  snprintf((out), (out_size), "block %u declares span [%u,%u), which does not move the walk forward", (p)->block_idx, (p)->start, (p)->end)
#define LOG_BODY_ERR_VM_EVENT_OVERFLOW(p, out, out_size)                                                     \
  snprintf((out), (out_size), "vm event buffer full at %u per cycle: %u dropped, latest of callback type %u", \
           (p)->depth, (p)->dropped, (p)->type)
/* Both expression tags name the byte offset of the *instruction*, not the
   cursor past its operand, so a trace points at what a disassembly shows.
   `reason` is a VM_EXPR_BAD_* / VM_EXPR_MATH_* code from vm_block_expr.h. */
#define VM_EXPR_BAD_NAME(r)                                                                                 \
  ((r) == 0 ? "header does not fit custom_data" : (r) == 1 ? "block has no output"                          \
   : (r) == 2 ? "unknown opcode" : (r) == 3 ? "bad operand"                                                 \
   : (r) == 4 ? "stack underflow" : (r) == 5 ? "stack overflow"                                             \
   : (r) == 6 ? "stack did not end with one value" : "?")
#define VM_EXPR_MATH_NAME(r) \
  ((r) == 0 ? "divide by zero" : (r) == 1 ? "operand outside domain" : (r) == 2 ? "result not finite" : "?")
#define LOG_BODY_ERR_VM_EXPR_BAD_CODE(p, out, out_size)                                                     \
  snprintf((out), (out_size), "block %u expression at pc %u (op %u): %s", (p)->block_idx, (p)->pc,          \
           (p)->opcode, VM_EXPR_BAD_NAME((p)->reason))
#define LOG_BODY_ERR_VM_EXPR_MATH(p, out, out_size)                                                         \
  snprintf((out), (out_size), "block %u expression at pc %u (op %u): %s", (p)->block_idx, (p)->pc,          \
           (p)->opcode, VM_EXPR_MATH_NAME((p)->reason))
/* A loop that could not end on its own condition -- a zero step, a step
   pointing away from the end, an iterator that overflowed. The turn budget
   caught it, which is what the budget is for; reporting it is how the program
   learns its loop was wrong rather than merely slow. `reason` is a
   VM_FOR_BAD_* code from vm_block_for.h. */
#define VM_FOR_BAD_NAME(r) ((r) == 0 ? "ran its whole turn budget" : (r) == 1 ? "iterator went non-finite" : "?")
#define LOG_BODY_ERR_VM_FOR_BAD_LOOP(p, out, out_size)                                                      \
  snprintf((out), (out_size), "block %u loop %s after %lu of %u turns", (p)->block_idx,                     \
           VM_FOR_BAD_NAME((p)->reason), (unsigned long)(p)->turns, (p)->cap)

/**
 * @brief SE_EMIT_ERR() relies on an ambient `#define OWNER` per source file
 * (see the [[runit]] skill's module owner tagging convention) - that doesn't
 * fit vm_obj_access.h/vm_obj.h, which are header-only and get pulled into
 * many different .c files, each with its own OWNER. This variant takes the
 * owner explicitly so it behaves the same regardless of include order or
 * whatever OWNER the including file has defined.
 */
#define SE_EMIT_ERR_OWNED(owner, tag_name, ...)                                                \
  do {                                                                                          \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));          \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};   \
    SE_push_to_handler(__e);                                                                    \
  } while (0)

// Same reasoning, for the return-style macros (SE_ERR_NEW / SE_RET_ERR /
// SE_CHECK_NOT_NULL / SE_CHECK_IF_ALLOCATED) - these build and *return* an
// err_h rather than pushing it, but still rely on the ambient OWNER token.
#define SE_ERR_NEW_OWNED(owner, tag_name, ...)                                                \
  ({                                                                                           \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));         \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};  \
    __e;                                                                                       \
  })

#define SE_RET_ERR_OWNED(owner, tag_name, ...) return SE_ERR_NEW_OWNED((owner), tag_name, __VA_ARGS__)

// Same reasoning again, for SE_WRAP_ERR - chains rc_err as the new node's
// next_cause instead of leaving it NULL, the same linked-cause shape
// SE_WRAP_DEV_ERR uses for device errors, just with an explicit owner.
#define SE_WRAP_ERR_OWNED(owner, rc_err, tag_name, ...)                                      \
  ({                                                                                          \
    err_h __new_err = SE_ERR_NEW_OWNED((owner), tag_name, __VA_ARGS__);                       \
    __new_err->next_cause = (rc_err);                                                         \
    __new_err;                                                                                \
  })

#define SE_CHECK_NOT_NULL_OWNED(owner, ptr)              \
  do {                                                   \
    if ((ptr) == NULL) {                                 \
      SE_RET_ERR_OWNED((owner), ERR_NULL_PTR, 0);        \
    }                                                    \
  } while (0)

#define SE_CHECK_IF_ALLOCATED_OWNED(owner, ptr)          \
  do {                                                   \
    if ((ptr) == NULL) {                                 \
      SE_RET_ERR_OWNED((owner), ERR_BASE_NO_MEM, 0);     \
    }                                                    \
  } while (0)
