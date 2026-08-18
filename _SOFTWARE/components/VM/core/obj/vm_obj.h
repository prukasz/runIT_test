#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include "esp_attr.h"
#include "esp_compiler.h"  // likely()/unlikely()

/*
Object used in VM, it poses pseudo JSON functionalities. withy limited TAG (15 chars), and payload with different types.
Object is suitable to store numerical values or other objects creating object tree.
Object can store one item or array of items. Count of items stored is defined by payload size and itme size.
Object support mixing  types in object tree creating JSON nested representation.
Object is accesed using handle (pointer)
Object data is stored as flexible array member, this means that object is required to poses size of Head (description) and space for all data and optional TAG
Object is unaware of parent in tree, every object can be treated as standalone instance
Object is accessed using accessor and id -> see related accessor files
*/

// Longest tag a 4-bit name_size can describe. (4-bit size in obj head)
#define VM_OBJ_NAME_MAX 15
/**
 * @brief Possible types of object items stored
 */
typedef enum vm_obj_t_e {
  VM_OBJ_NONE = 0,
  VM_OBJ_PTR = 1,  // ptr at object
  VM_OBJ_U8 = 2,
  VM_OBJ_U32 = 3,
  VM_OBJ_I32 = 4,
  VM_OBJ_F = 5,
  VM_OBJ_B = 6,
  VM_OBJ_STR = 7,
  VM_OBJ_U64 = 8,
} vm_obj_t_e;

/**
 * @brief Size of type lookup
 */
static const uint8_t vm_obj_type_sizes[] = {
    [VM_OBJ_NONE] = 0,
    [VM_OBJ_PTR] = sizeof(void*),
    [VM_OBJ_U8] = sizeof(uint8_t),
    [VM_OBJ_U32] = sizeof(uint32_t),
    [VM_OBJ_I32] = sizeof(int32_t),
    [VM_OBJ_F] = sizeof(float),
    [VM_OBJ_B] = sizeof(uint8_t),
    [VM_OBJ_STR] = sizeof(uint8_t),
    [VM_OBJ_U64] = sizeof(uint64_t),
};

/**
 * @brief Shift to get payload items count from payload size
 */
static const uint8_t vm_obj_type_shifts[] = {
    [VM_OBJ_NONE] = 0,
    [VM_OBJ_PTR] = 2,
    [VM_OBJ_U8] = 0,
    [VM_OBJ_U32] = 2,
    [VM_OBJ_I32] = 2,
    [VM_OBJ_F] = 2,
    [VM_OBJ_B] = 0,
    [VM_OBJ_STR] = 0,
    [VM_OBJ_U64] = 3,
};
_Static_assert(sizeof(void*) == 4, "vm_obj_type_shifts assumes 4-byte pointers");

/*
The same shift table packed two bits per type into one immediate, because the
array form costs a data load the hot path cannot afford.

`vm_obj_type_shifts[t]` compiles to an `l32r` for the table address plus an
`l8ui` -- and .rodata lives in flash, so that load goes through the data cache
on every single element access. The packed form is a literal: it rides the
instruction path with the code that uses it and never touches the data side.
`vm_obj_type_sizes` disappears from the hot path too, since every valid width
is exactly `1 << shift`.

Both arrays stay for the cold paths and for the self test, which checks the
pack against them entry by entry (stage A) -- an array element is not an
integer constant expression, so this cannot be a _Static_assert.

  type:  NONE PTR U8 U32 I32  F  B STR U64
  shift:    0   2   0   2   2  2  0   0   3
*/
#define VM_OBJ_SHIFT_PACK 0x00030A88u

/** @brief Element-size shift for a type id. Meaningless unless vm_type_ok(). */
static __always_inline uint32_t vm_type_shift(uint8_t t) {
  return (VM_OBJ_SHIFT_PACK >> (t << 1)) & 3u;
}

/** @brief Is `t` a real type with a width -- i.e. 1..VM_OBJ_U64, not NONE and
 *  not one of the unused 4-bit encodings. One compare, unsigned wrap doing
 *  both ends at once. */
static __always_inline bool vm_type_ok(uint8_t t) {
  return (uint8_t)(t - 1u) <= (uint8_t)(VM_OBJ_U64 - 1u);
}

/**
 * @brief Object head describing every existing object.
 */
typedef struct __attribute__((aligned(4))) vm_obj_head_t {
  uint16_t payload_size;  // value length in bytes -- payload[0..payload_size) holds a single value or an array, data_size = N * type_size for an array
  struct {
    uint8_t obj_t : 4;      // what type is stored
    uint8_t name_size : 4;  // up to 15 chars, payload[payload_size]+name[name_size]
  } d;
  struct {
    uint8_t mutable : 1;        // is value editable by any one
    uint8_t upd : 1;            // has value been updated / refreshed lately
    uint8_t upd_resetable : 1;  // can flag be reset
    uint8_t tagged : 1;         // is name field populated
    uint8_t retentive : 1;      // should be stored in nvs - requires type of non-prt
    uint8_t dynamic : 1;        // heap-allocated behind a vm_dyn_hdr_t, freed at refcount zero -- see vm_obj_dyn.h
    uint8_t _pad : 2;
  } f;
} vm_obj_head_t;

_Static_assert(sizeof(vm_obj_head_t) == 4, "vm_obj_head_t must stay 4 bytes");

/**
 * @brief vm_object consisit of head - always present object descriptor and flexible array memeber
 * data with declared size in head - it stores object body
 */
typedef struct vm_obj_t {
  vm_obj_head_t head;
  uint8_t payload[];
} vm_obj_t;

/**
 * @brief Object handle used across all files
 */
typedef vm_obj_t* vm_obj_h;

_Static_assert(offsetof(vm_obj_t, payload) == 4, "payload must follow the head with no padding");

/**
 * @brief Object creation and descriptor flags
 */
#define VM_OBJ_F_MUTABLE 0x01
#define VM_OBJ_F_UPD_RESETABLE 0x02
#define VM_OBJ_F_RETENTIVE 0x04

#define VM_LOAD_F_MUTABLE VM_OBJ_F_MUTABLE
#define VM_LOAD_F_UPD_RESETABLE VM_OBJ_F_UPD_RESETABLE
#define VM_LOAD_F_RETENTIVE VM_OBJ_F_RETENTIVE

/**
 * @brief Construct a vm_obj_head_t descriptor from parameters.
 *
 * Automatically computes payload_size from item_count and type width.
 *
 * @param type vm_obj_t_e element type (e.g. VM_OBJ_F, VM_OBJ_U32, VM_OBJ_PTR).
 * @param item_count Number of elements (scalar is 1).
 * @param flags Flag bits (VM_OBJ_F_MUTABLE, etc.).
 * @param name_len Length of tag (0..15).
 */
static __always_inline vm_obj_head_t vm_obj_head(vm_obj_t_e type, uint16_t item_count, uint8_t flags,
                                                 uint8_t name_len) {
  vm_obj_head_t h = {0};
  h.payload_size = (uint16_t)(item_count << vm_type_shift((uint8_t)type));
  h.d.obj_t = (uint8_t)type;
  h.d.name_size = (uint8_t)(name_len > VM_OBJ_NAME_MAX ? VM_OBJ_NAME_MAX : name_len);
  h.f.mutable = (flags & VM_OBJ_F_MUTABLE) != 0;
  h.f.upd_resetable = (flags & VM_OBJ_F_UPD_RESETABLE) != 0;
  h.f.retentive = (flags & VM_OBJ_F_RETENTIVE) != 0;
  return h;
}

/**
 * @brief Total size that is alocated, required to use as tag also count to total size
 */
static __always_inline uint32_t vm_obj_total_size(vm_obj_h obj) {
  return (uint32_t)sizeof(vm_obj_head_t) + obj->head.payload_size + obj->head.d.name_size;
}

/**
 * @brief Payload size getter
 */
static __always_inline uint16_t vm_obj_payload_size(vm_obj_h obj) {
  return obj->head.payload_size;
}

static __always_inline uint8_t vm_type_width(vm_obj_t_e t) {
  return ((uint8_t)t < sizeof(vm_obj_type_sizes) / sizeof(vm_obj_type_sizes[0])) ? vm_obj_type_sizes[t] : 0;
}

/**
 * @brief Safe object item size getter
 */
static __always_inline uint8_t vm_obj_type_size(vm_obj_h obj) {
  return vm_type_width((vm_obj_t_e)obj->head.d.obj_t);
}

/**
 * @brief Total items count calculated from payload size and object type size
 */
static __always_inline uint16_t vm_obj_items_cnt(vm_obj_h obj) {
  uint8_t t = obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t))) return 0;
  return (uint16_t)(vm_obj_payload_size(obj) >> vm_type_shift(t));
}

/**
 * @brief Address of element `i`, or NULL if `i` is past the end or the object
 *        has no usable type.
 *
 * Works in byte offsets rather than item counts. `off = i << shift` gives the
 * address, and comparing that same `off` against payload_size gives the bounds
 * check -- so one shift does both. Deriving a count first and then multiplying
 * by the width costs an extra shift, a table load for the width, and a `mull`
 * (5 cycles on Xtensa) to arrive at the identical address.
 *
 * `i` is 32-bit: a by-ref index comes out of an object and can hold anything
 * the program put there. The >UINT16_MAX rejection is what keeps `i << 3` from
 * wrapping into a small offset and turning a wild index into a silent in-range
 * read -- payload_size is 16-bit, so nothing legitimate is lost.
 */
static __always_inline uint8_t* vm_obj_elem_ptr(vm_obj_h obj, uint32_t i) {
  uint8_t t = obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t) || i > UINT16_MAX)) return NULL;
  uint32_t off = i << vm_type_shift(t);
  if (unlikely(off >= obj->head.payload_size)) return NULL;
  return obj->payload + off;
}

/**
 * @brief Tag getter
 */
static __always_inline const char* vm_obj_tag(vm_obj_h obj, uint8_t* out_len) {
  if (!obj->head.f.tagged) return NULL;
  *out_len = obj->head.d.name_size;
  return (const char*)(obj->payload + obj->head.payload_size);
}

/**
 * @brief Payload getter as uint8_t ptr
 */
static __always_inline uint8_t* vm_obj_payload(vm_obj_h obj) {
  return obj->head.payload_size != 0 ? obj->payload : NULL;
}
