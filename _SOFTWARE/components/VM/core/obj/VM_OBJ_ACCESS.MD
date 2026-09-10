# VM Object Subsystem (`vm_obj`, `vm_obj_access`, `vm_obj_build`, `vm_obj_dyn`) — Public API Reference & Examples

This document details the complete public API surface of the VM Object layer:
- `vm_obj.h`: Object representation, packed headers, geometry getters, and element access.
- `vm_obj_access.h`: Accessor resolution, payload slicing, scalar read/write, copy/clone, linking, and user space security boundary.
- `vm_obj_build.h`: Shape validation, arena object creation, and chained accessor construction.
- `vm_obj_dyn.h`: Heap-allocated dynamic objects, DAG ownership validation, and reference counting.

Lifetime, ownership graphs, failure atomicity, and freshness contracts are defined in [VM_OBJ_CONTRACTS.md](VM_OBJ_CONTRACTS.md).

---

## 1. API Conventions

All object subsystem APIs adhere to a consistent operational naming scheme:
- **`make` (`vm_make_*`)**: Constructors, initializers, and slice wrappers that assemble descriptors from arguments.
- **`get` (`vm_obj_get_*`, `vm_*_get_*`, `vm_obj_dyn_get_*`)**: Non-destructive readers for geometry, headers, raw pointers, registry lookup, and accessor resolution.
- **`set` (`vm_obj_set_*`, `vm_accessor_set_*`)**: Mutating setters that write values, copy/clone data, or configure descriptors.
- **`_usr` suffix**: Security boundary functions enforcing that the target object is mutable (`VM_OBJ_F_MUTABLE`) and not user-protected (`!VM_OBJ_F_USR_PROTECTED`).
- **Accessor Resolution**: Single-operation resolution (`vm_obj_get_payload`, `vm_obj_get_obj`, `vm_obj_get_owner`) through cached 4-load paths or chained dereferences.
- **Macros (`VM_OBJ_*_VAL*`)**: Fast typed scalar getters and setters converting between C types and object storage.

---

## 2. Types & Constants Summary

### Types

| Type | Declared In | Purpose | Size |
| :--- | :--- | :--- | :--- |
| `vm_obj_t_e` | `vm_obj.h` | Element type enum (`VM_OBJ_NONE`, `_PTR`, `_U8`, `_U32`, `_I32`, `_F`, `_B`, `_STR`, `_U64`) | 1 byte |
| `vm_obj_head_t` | `vm_obj.h` | Packed header descriptor: `payload_size`, `obj_t`, `name_size`, and flag bits | 4 bytes |
| `vm_obj_t` / `vm_obj_h` | `vm_obj.h` | Object struct (header + flexible payload array) and its pointer handle | Header + N |
| `vm_payload_t` | `vm_obj_access.h` | Resolved view: `ptr` (target memory), `count` (items), `type` (`vm_obj_t_e`) | 8 bytes |
| `vm_val_t` | `vm_obj_access.h` | Unified scalar value union (`.f`, `.i32`, `.u32`, `.u64`, `.b`, `.u8`) | 8 bytes |
| `vm_accessor_t` | `vm_obj_access.h` | Root object ID + index chain + inline resolution cache | 20 bytes |
| `vm_index_t` | `vm_obj_access.h` | Index step (`VM_IDX_LITERAL`, `VM_IDX_REF`, `VM_IDX_NAME`) | 8 bytes |
| `vm_resolved_t` | `vm_obj_access.h` | Resolution result: `{ vm_payload_t payload; vm_obj_h owner; }` | 12 bytes |
| `vm_obj_dyn_meta_t` | `vm_obj_dyn.h` | Dynamic table entry: `uint32_t ref_cnt; vm_obj_h obj;` | 8 bytes |

### Flags & Limits

| Symbol | Defined In | Meaning |
| :--- | :--- | :--- |
| `VM_OBJ_F_MUTABLE` | `vm_obj.h` | Object allows runtime modification |
| `VM_OBJ_F_USR_PROTECTED` | `vm_obj.h` | User script writes denied; internal supervisor writes allowed |
| `VM_OBJ_F_UPD_RESETABLE` | `vm_obj.h` | Freshness flag (`f.upd`) is cleared at pass cycle end |
| `VM_OBJ_F_RETENTIVE` | `vm_obj.h` | Preserved across warm reboots |
| `VM_OBJ_NAME_MAX` | `vm_obj.h` | Max tag length (15 bytes) |
| `VM_ACCESSOR_MAX_DEPTH`| `vm_obj_access.h`| Chain recursion cap (8) |
| `VM_DYN_MAX` | `vm_obj_dyn.h` | Maximum live dynamic objects on the heap (128) |
| `VM_DYN_MAX_DEPTH` | `vm_obj_dyn.h` | Maximum ownership path depth in dynamic DAG (16) |
| `VM_DYN_NO_ID` | `vm_obj_dyn.h` | Return value when object is not in dynamic register (0xFFFF) |

---

## 3. Subsystem Function Catalogs

### A. Object Geometry & Inspection (`vm_obj.h`)
- `vm_make_obj_head(type, item_count, flags, name_len)`: Construct header with computed `payload_size`.
- `vm_obj_get_payload_size(obj)`: Total payload bytes.
- `vm_obj_get_type_size(obj)`: Byte width of one element.
- `vm_obj_get_items_cnt(obj)`: Element count (bitshift derived).
- `vm_obj_get_total_size(obj)`: Allocated footprint (head + payload + tag).
- `vm_obj_get_payload_ptr(obj)`: Raw pointer to `payload[0]`.
- `vm_obj_get_elem_ptr(obj, i)`: Bounds-checked pointer to element `i`.
- `vm_obj_get_tag(obj, out_len)`: Tag name string and length.

### B. Object & Accessor Construction (`vm_obj_build.h`)
- `vm_obj_create(out, id, head, name)`: Bump-allocates in program arena, zeroes, and binds registry ID.
- `vm_obj_shape(head, out_total)`: Validates head descriptor and computes total bytes needed.
- `vm_obj_init(o, head, name)`: Initializes allocated memory with header and name.
- `vm_accessor_create(out, id, root_obj_id, idx_count)`: Allocates accessor and trailing index array.
- `vm_accessor_set_literal(acc, pos, value)`: Sets index step to literal index value.
- `vm_accessor_set_ref(acc, pos, ref)`: Sets index step to read position live from another accessor.
- `vm_accessor_set_name(acc, pos, name, name_len)`: Sets index step to match child tag by name.
- `vm_accessor_cache_build(acc)`: Pre-resolves static accessors (4 loads instead of walking).

### C. Accessor Resolution & Data Operations (`vm_obj_access.h`)
- `vm_obj_get_by_id(id)`: Registry lookup for arena objects.
- `vm_accessor_get_by_id(id)`: Registry lookup for accessors.
- `vm_obj_get_payload(target, source)`: Resolves accessor to destination payload view.
- `vm_obj_get_obj(target, source)`: Resolves accessor to underlying object handle.
- `vm_obj_get_owner(target, source)`: Resolves object owning addressed memory.
- `vm_obj_get_child(parent, tag)`: Discovers child in PTR container by tag string.
- `vm_payload_get_at(p, i)`: Bounds-checked 1-element slice of payload `p` at index `i`.
- `vm_make_payload(obj)`: Wraps whole object into `vm_payload_t`.
- `vm_obj_set_scalar(target, v, src_type)`: Writes converted scalar through an accessor.
- `vm_obj_set_scalar_direct(obj, index, v, src_type)`: Writes converted scalar straight to `obj[index]`.
- `vm_obj_copy_content(source, target)`: Shape-validated deep copy without allocation.
- `vm_obj_clone_into(source, target)`: Re-allocates schema dynamically if differing, then copies.
- `vm_obj_link(to_join, owner)`: Links accessor-addressed PTR slot to object.
- `vm_obj_link_direct(cell, index, child)`: Points element `index` of PTR `cell` directly to `child`.
- `vm_obj_publish(obj)`: Marks object aggregate freshness (`f.upd = 1`).

### D. Dynamic Heap Objects (`vm_obj_dyn.h`)
- `vm_obj_is_dynamic(o)`: Tests if object is heap-allocated and reference-counted.
- `vm_obj_dyn_get_id(o)`: Returns dynamic registry index or `VM_DYN_NO_ID`.
- `vm_obj_dyn_get_by_id(id)`: Returns dynamic object handle at registry index `id`.
- `vm_obj_dyn_create(out, head, name)`: Allocates dynamic object on heap with refcount 0.
- `vm_obj_dyn_retain(o)`: Increments reference count (no-op on arena objects).
- `vm_obj_dyn_release(o)`: Decrements refcount; frees and cascades to dynamic children at zero.
- `vm_obj_dyn_check_link(owner, cell, child)`: Validates DAG rules (depth <= 16, no cycles).
- `vm_obj_dyn_reset()`: Frees all registered dynamic objects during loader teardown.

---

## 4. Practical Code Examples

### Example 1: Header Construction & Object Allocation (`vm_make_obj_head`, `vm_obj_create`)

```c
#include "vm_obj.h"
#include "vm_obj_build.h"

// 1. Construct header descriptor for a float array of 4 items with tag "PID_GAINS"
const char* tag = "PID_GAINS";
uint8_t tag_len = strlen(tag);

vm_obj_head_t head = vm_make_obj_head(
    VM_OBJ_F,               // Type: 32-bit float
    4,                      // Count: 4 elements (payload_size will be 16 bytes)
    VM_OBJ_F_MUTABLE,       // Flags: allow writes
    tag_len                 // Name length: capped at VM_OBJ_NAME_MAX (15)
);

// 2. Allocate object in the program arena
vm_obj_h gains_obj = NULL;
err_h err = vm_obj_create(&gains_obj, 10, &head, tag);
if (err != ERR_OK) {
    return err; // E.g. ERR_BASE_NO_MEM or ERR_VM_REG_DUP
}
```

---

### Example 2: Inspecting Geometry & Metadata (`vm_obj_get_*`)

```c
#include "vm_obj.h"

void inspect_object(vm_obj_h obj) {
    if (!obj) return;

    uint16_t count = vm_obj_get_items_cnt(obj);      // Fast log2 bitshift
    uint16_t bytes = vm_obj_get_payload_size(obj);   // Exact payload byte count
    uint8_t elem_sz = vm_obj_get_type_size(obj);     // Element width in bytes
    uint32_t total = vm_obj_get_total_size(obj);     // sizeof(head) + payload + tag

    uint8_t tag_len = 0;
    const char* tag = vm_obj_get_tag(obj, &tag_len);

    printf("Object '%.*s': %u elements (%u bytes, %u B/elem), total size: %lu B\n",
           tag_len, tag ? tag : "UNTAGGED", count, bytes, elem_sz, (unsigned long)total);
}
```

---

### Example 3: Direct Memory & Element Access (`vm_obj_get_elem_ptr`, `vm_obj_get_payload_ptr`)

```c
#include "vm_obj.h"

bool calculate_float_average(vm_obj_h obj, float* out_avg) {
    if (!obj || obj->head.d.obj_t != VM_OBJ_F) return false;

    uint16_t count = vm_obj_get_items_cnt(obj);
    if (count == 0) return false;

    float sum = 0.0f;
    for (uint16_t i = 0; i < count; i++) {
        float* p_val = (float*)vm_obj_get_elem_ptr(obj, i);
        if (!p_val) return false; // Bounds check failed
        sum += *p_val;
    }

    *out_avg = sum / (float)count;
    return true;
}
```

---

### Example 4: Registry Lookups & Accessor Resolution (`vm_*_get_by_id`, `vm_obj_get_*`)

```c
#include "vm_obj_access.h"

err_h read_sensor_channel(uint16_t accessor_id, float* out_reading) {
    // 1. Look up accessor by ID
    const vm_accessor_t* acc = vm_accessor_get_by_id(accessor_id);
    if (!acc) return ERR_VM_OBJ_NOT_FOUND;

    // 2. Resolve accessor to target payload
    vm_payload_t target_payload;
    err_h err = vm_obj_get_payload(&target_payload, acc);
    if (err != ERR_OK) return err;

    // 3. Resolve owner object holding the bytes
    vm_obj_h owner = NULL;
    err = vm_obj_get_owner(&owner, acc);
    if (err == ERR_OK && owner) {
        // Inspect owner flags or metadata if needed
    }

    // 4. Extract converted scalar
    *out_reading = vm_get_as_f32(target_payload.type, target_payload.ptr);
    return ERR_OK;
}
```

---

### Example 5: High-Level Value Read & Write Macros (`VM_OBJ_GET_VAL`, `VM_OBJ_SET_VAL`)

```c
#include "vm_obj_access.h"

void process_control_loop(const vm_accessor_t* sensor_acc, const vm_accessor_t* actuator_acc) {
    float sensor_val = 0.0f;

    // Read scalar from sensor accessor into sensor_val (converts from any numeric type)
    err_h err = VM_OBJ_GET_VAL(sensor_val, sensor_acc);
    if (err != ERR_OK) return;

    float control_val = sensor_val * 1.5f;

    // Write scalar to actuator accessor (marks owner as updated)
    err = VM_OBJ_SET_VAL(control_val, actuator_acc);
    if (err != ERR_OK) return;
}
```

---

### Example 6: Slicing Payloads & Iteration (`vm_make_payload`, `vm_payload_get_at`)

```c
#include "vm_obj_access.h"

err_h find_max_value(vm_obj_h obj, float* out_max) {
    if (!obj) return ERR_VM_INVALID_ARG;

    // Wrap whole object into a payload slice
    vm_payload_t slice = vm_make_payload(obj);
    if (slice.count == 0) return ERR_VM_INVALID_ARG;

    float max_val = -1e9f;
    for (uint16_t i = 0; i < slice.count; i++) {
        vm_payload_t elem = vm_payload_get_at(slice, i);

        vm_val_t val;
        VM_PAYLOAD_GET_VAL(val, elem);

        float current = vm_get_as_f32(elem.type, (const uint8_t*)&val);
        if (current > max_val) max_val = current;
    }

    *out_max = max_val;
    return ERR_OK;
}
```

---

### Example 7: Direct Writes & Explicit Publication (`VM_OBJ_SET_VAL_AT`, `vm_obj_publish`)

```c
#include "vm_obj_access.h"

err_h update_filter_coefficients(vm_obj_h filter_obj, float k1, float k2) {
    if (!filter_obj) return ERR_VM_INVALID_ARG;

    // Direct inline write by element index
    err_h err = VM_OBJ_SET_VAL_AT(k1, filter_obj, 0);
    if (err != ERR_OK) return err;

    err = VM_OBJ_SET_VAL_AT(k2, filter_obj, 1);
    if (err != ERR_OK) return err;

    // Explicit aggregate publication
    return vm_obj_publish(filter_obj);
}
```

---

### Example 8: Linking Pointers & Structuring Trees (`vm_obj_link_direct`, `vm_obj_get_child`)

```c
#include "vm_obj_access.h"

err_h build_device_tree(vm_obj_h container_ptr_obj, vm_obj_h motor_obj, vm_obj_h encoder_obj) {
    // 1. Link child objects into container slots directly
    err_h err = vm_obj_link_direct(container_ptr_obj, 0, motor_obj);
    if (err != ERR_OK) return err;

    err = vm_obj_link_direct(container_ptr_obj, 1, encoder_obj);
    if (err != ERR_OK) return err;

    // 2. Discover child object by tag name
    vm_obj_h child = vm_obj_get_child(container_ptr_obj, "MOTOR");
    if (child == motor_obj) {
        // Matched child
    }

    return ERR_OK;
}
```

---

### Example 9: Deep Copy vs Dynamic Cloning (`vm_obj_copy_content`, `vm_obj_clone_into`)

```c
#include "vm_obj_access.h"

// Scenario A: Fast in-place copy (zero memory allocation; fails if shapes differ)
err_h copy_buffer_state(const vm_accessor_t* src_acc, const vm_accessor_t* dst_acc) {
    return vm_obj_copy_content(src_acc, dst_acc);
}

// Scenario B: Dynamic cloning (automatically allocates/replaces schema if differing)
err_h clone_sensor_packet(const vm_accessor_t* packet_acc, const vm_accessor_t* storage_acc) {
    return vm_obj_clone_into(packet_acc, storage_acc);
}
```

---

### Example 10: Programmatic Accessor Construction & Caching (`vm_accessor_create`, `vm_accessor_cache_build`)

```c
#include "vm_obj_build.h"

err_h create_motor_speed_accessor(uint16_t acc_id, uint16_t motor_obj_id) {
    vm_accessor_t* acc = NULL;

    // 1. Allocate accessor with 1 index step
    err_h err = vm_accessor_create(&acc, acc_id, motor_obj_id, 1);
    if (err != ERR_OK) return err;

    // 2. Configure index 0 to point to element 2 (e.g. speed channel)
    err = vm_accessor_set_literal(acc, 0, 2);
    if (err != ERR_OK) return err;

    // 3. Pre-build resolve cache (4 loads instead of full chain walk at runtime)
    vm_accessor_cache_build(acc);

    return ERR_OK;
}
```

---

### Example 11: Dynamic Heap Objects & Reference Counting (`vm_obj_dyn_*`)

```c
#include "vm_obj_dyn.h"

err_h handle_dynamic_message(vm_obj_h* out_dyn_msg) {
    // 1. Create a dynamic heap object
    vm_obj_head_t head = vm_make_obj_head(VM_OBJ_U8, 64, VM_OBJ_F_MUTABLE, 0);
    vm_obj_h dyn_obj = NULL;
    err_h err = vm_obj_dyn_create(&dyn_obj, &head, NULL);
    if (err != ERR_OK) return err;

    // 2. Query registry ID
    uint16_t slot_id = vm_obj_dyn_get_id(dyn_obj);
    assert(slot_id != VM_DYN_NO_ID);

    // 3. Round-trip lookup by ID
    vm_obj_h lookup = vm_obj_dyn_get_by_id(slot_id);
    assert(lookup == dyn_obj);

    // 4. Retain and release
    vm_obj_dyn_retain(dyn_obj);  // ref_cnt: 0 -> 1
    vm_obj_dyn_release(dyn_obj); // ref_cnt: 1 -> 0 (freed immediately)

    return ERR_OK;
}
```

---

### Example 12: User Space Protection Boundary (`_usr` APIs)

```c
#include "vm_obj_access.h"

err_h user_script_write(const vm_accessor_t* target_acc, float value) {
    // Automatically rejects writes if target has VM_OBJ_F_USR_PROTECTED set
    err_h err = VM_OBJ_SET_VAL_USR(value, target_acc);
    if (err == ERR_VM_OBJ_USR_PROTECTED) {
        // Permission denied: protected variable cannot be written by user block
        return err;
    }
    return ERR_OK;
}
```
