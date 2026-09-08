# vm_obj.h + vm_obj_access.h — Public API Reference

Lifetime, permissions, failure atomicity, and freshness are defined in
[VM_OBJ_CONTRACTS.md](VM_OBJ_CONTRACTS.md).

Internal-only helpers are left out: `vm_obj.h`'s two lookup tables, and
`vm_obj_access.h`'s raw scalar-conversion primitives (`vm_payload_read`,
`vm_f_to_i`, `VM_VAL_CAST_TO`, `vm_read_as_f32/i32/i64`, `VM_LOAD_CAST_TO`,
`VM_LOAD_CAST_TO_FAST`, `VM_TYPE_OF`, `VM_VAL_OF`, `vm_store_inline`) — none
of these are meant to be called directly; they exist to back
`VM_OBJ_GET_VAL`/`VM_OBJ_SET_VAL`/`VM_OBJ_SET_VAL_AT`/`VM_PAYLOAD_GET_VAL`.

## vm_obj.h — Object Layer

### Types

| Type | What it holds |
| :--- | :--- |
| `vm_obj_t_e` | Element type tag: `VM_OBJ_PTR`, `_U8`, `_U32`, `_I32`, `_F`, `_B`, `_STR`, `_U64`. |
| `vm_obj_head_t` | 4-byte descriptor: `payload_size`, type + name length, flag bits. |
| `vm_obj_t` / `vm_obj_h` | The object itself (head + flexible-array payload), and its handle. |

### Constants & Flags

| Symbol | Meaning |
| :--- | :--- |
| `VM_OBJ_NAME_MAX` | Longest tag a 4-bit `name_size` can hold (15). |
| `VM_OBJ_F_MUTABLE` / `_UPD_RESETABLE` / `_RETENTIVE` / `_USR_PROTECTED` | Flags for `vm_obj_head()`'s `flags` param. |
| `VM_LOAD_F_MUTABLE` / `_UPD_RESETABLE` / `_RETENTIVE` | Same bits, wire-side names. |

### Functions

| Function | What it does |
| :--- | :--- |
| `vm_type_shift(t)` | Element-size shift for a type id; 0 if `t` is not a real type. |
| `vm_type_ok(t)` | True if `t` is a real type: `VM_OBJ_PTR`..`VM_OBJ_U64`. |
| `vm_obj_head(type, item_count, flags, name_len)` | Construct a `vm_obj_head_t`, `payload_size` computed automatically. |
| `vm_obj_total_size(obj)` | Bytes allocated: head + payload + tag. |
| `vm_obj_payload_size(obj)` | Payload size in bytes. |
| `vm_type_width(t)` | Byte width of one element of type `t`. |
| `vm_obj_type_size(obj)` | Byte width of one element of `obj`'s type. |
| `vm_obj_items_cnt(obj)` | Element count, from payload size and type width. |
| `vm_obj_elem_ptr(obj, i)` | Address of element `i`, or NULL if out of range or type invalid. |
| `vm_obj_tag(obj, out_len)` | Object's tag string, or NULL if untagged. |
| `vm_obj_payload(obj)` | Payload as `uint8_t*`, or NULL if empty. |

## vm_obj_access.h — Access & Resolution Layer

### Types

| Type | What it holds |
| :--- | :--- |
| `vm_payload_t` | Resolved value location: `ptr`, `count`, `type` (8 bytes). |
| `vm_val_t` | Scratch union wide enough for any scalar `vm_obj_t_e`. |
| `vm_accessor_t` | Root id + chained `vm_index_t[]` + resolve cache (20 bytes). |
| `vm_index_t` / `vm_index_kind_e` | One chain step: `VM_IDX_LITERAL`, `VM_IDX_REF`, or `VM_IDX_NAME`. |
| `vm_resolved_t` | `{ payload, owner }` — what a resolve produces. |

### Flags & Limits

| Symbol | Meaning |
| :--- | :--- |
| `VM_ACC_F_CACHED` | `vm_accessor_t.flags` bit: payload is cached in `c_owner`/`c_payload`. |
| `VM_ACCESSOR_MAX_DEPTH` | Chain / by-ref recursion cap (8). |
| `VM_IDX_BY_NAME(str)` | Build a by-name `vm_index_t` initializer. |
| `VM_OBJ_COPY_MAX_DEPTH` | Pointer-tree walk depth cap for copy/clone (8). |
| `VM_COPY_SHAPE_DEPTH` / `_SRC_EMPTY` / `_DST_EMPTY` | `reason` codes for `ERR_VM_OBJ_COPY_SHAPE`. |

### Functions

| Function | What it does |
| :--- | :--- |
| `vm_obj_by_id(id)` | Object handle for a registry id, or NULL. |
| `vm_accessor_by_id(id)` | Accessor handle for a registry id, or NULL. |
| `vm_payload_at(p, i)` | Element `i` of an already-resolved payload, bounds-checked. |
| `vm_obj_as_payload(obj)` | A whole object as a payload. |
| `vm_resolve_fast(acc, for_write, out)` | Inline resolve for cached/root/shallow accessors; `false` on miss (fall back to the entries below). |
| `vm_obj_get_payload(target, source)` | Resolve an accessor to where its value lives. |
| `vm_obj_get_owner(target, source)` | Object owning the addressed bytes; does not follow a trailing pointer slot. |
| `vm_get_obj(target, source)` | Resolve an accessor to the object itself (follows a trailing PTR). |
| `vm_obj_find_child(parent, tag)` | Child of a PTR array by tag, or NULL. |
| `vm_obj_copy_content(source, target)` | Copy one payload/tree into another of matching shape. |
| `vm_obj_shape_matches(a, b)` | Do two trees agree on type/count/wiring at every level? |
| `vm_obj_schema_matches(a, b)` | Storage shape plus tag identity at every level; Clone's reuse predicate. |
| `vm_obj_clone_shape(out, src)` | Allocate a dynamic tree shaped like `src`, values left zero. |
| `vm_obj_clone_into(source, target)` | Refill a matching schema, or build and fill before publishing its replacement. |
| `vm_obj_set_scalar(target, v, src_type)` | Resolve an accessor and write one converted scalar. |
| `vm_obj_set_scalar_direct(obj, index, v, src_type)` | Write one converted scalar straight into an owned object. |
| `vm_obj_clear_quiet(obj)` | Zero a non-PTR object's payload without setting `upd`. |
| `vm_obj_link(to_join, owner)` | Point an accessor-resolved PTR slot at another accessor's object. |
| `vm_obj_link_direct(cell, index, child)` | Point element `index` of `cell` at `child` directly. |
| `vm_obj_set_scalar_usr()` / `vm_obj_set_scalar_direct_usr()` | Scalar writes requiring mutable and not user-protected. |
| `vm_obj_copy_content_usr()` / `vm_obj_clone_into_usr()` / `vm_obj_link_usr()` | User mutation boundary used by user-authored blocks. |
| `vm_obj_publish(obj)` / `vm_obj_publish_usr(obj)` | Explicit aggregate freshness after a successful update; honors write policy. |

### Macros — the everyday calling surface

| Macro | What it does |
| :--- | :--- |
| `VM_OBJ_GET_VAL(output, source)` | Resolve `source` and store one converted scalar into `output`. |
| `VM_OBJ_SET_VAL(source, target)` | Resolve `target` and write `source` into it, converting. |
| `VM_OBJ_SET_VAL_AT(source, obj, index)` | Write `source` straight into `obj[index]`. |
| `VM_OBJ_SET_VAL_USR(source, target)` / `VM_OBJ_SET_VAL_AT_USR(source, obj, index)` | User-write counterparts requiring an unprotected target. |
| `VM_PAYLOAD_GET_VAL(output, payload)` | Convert an already-resolved payload into `output`. |
