# Packet Format Documentation

## Template for Variable-Length Packets

Use this template for packets with the structure:
```
[ctx_id:u8][type:u8][count:u8] + count×{[instance_idx:u16][start_idx:u16][items:u16] + items×[data]}
```

---

## Packet Name: [PACKET_NAME]

**Structure:**
```
[ctx_id:u8][type:u8][count:u8][data_blocks...]
```

**Header (3 bytes):**
- `ctx_id` (u8): Context ID
- `type` (u8): Data type (size: X bytes)
- `count` (u8): Number of blocks

**Data Block (repeated `count` times):**
```
[instance_idx:u16][start_idx:u16][items:u16][data:type*items]
```
- `instance_idx` (u16): Instance identifier
- `start_idx` (u16): Start index
- `items` (u16): Item count
- `data`: Array of `items` elements, each `sizeof(type)` bytes

**Total Size:** `3 + count × (6 + items × sizeof(type))` bytes

**Quick Reference:**
```
PKT_NAME: [u8 ctx][u8 type][u8 cnt] + cnt×{[u16 inst][u16 start][u16 items] + items×[type]}
```

---

## Packet Name: CREATE_CONTEXT

**Structure:**
```
[context_id:u8][data_blocks...]
```

**Header (1 byte):**
- `context_id` (u8): Context identifier

**Data Block (repeated 9 times for TYPES_CNT):**
```
[heap_elements:u32][max_instances:u16][max_dims:u16]
```
- `heap_elements` (u32): Number of heap elements for this type
- `max_instances` (u16): Maximum instances allowed
- `max_dims` (u16): Maximum dimensions

**Total Size:** `1 + 9 × (4 + 2 + 2)` = `73` bytes

**Quick Reference:**
```
CREATE_CTX: [u8 ctx_id] + 9×{[u32 heap][u16 max_inst][u16 max_dims]}
```

**Function:** `emu_mem_parse_create_context()`

---

## Packet Name: FILL_INSTANCE_SCALAR

**Structure:**
```
[ctx_id:u8][type:u8][count:u8][data_blocks...]
```

**Header (3 bytes):**
- `ctx_id` (u8): Context ID
- `type` (u8): Data type (size: varies)
- `count` (u8): Number of instances to fill

**Data Block (repeated `count` times):**
```
[inst_idx:u16][data:type]
```
- `inst_idx` (u16): Instance index
- `data`: Single value of `sizeof(type)` bytes

**Total Size:** `3 + count × (2 + sizeof(type))` bytes

**Quick Reference:**
```
FILL_SCALAR: [u8 ctx][u8 type][u8 cnt] + cnt×{[u16 inst][type data]}
```

**Function:** `emu_mem_fill_instance_scalar()`

---

## Packet Name: FILL_INSTANCE_ARRAY

**Structure:**
```
[ctx_id:u8][type:u8][count:u8][data_blocks...]
```

**Header (3 bytes):**
- `ctx_id` (u8): Context ID
- `type` (u8): Data type (size: varies)
- `count` (u8): Number of data blocks

**Data Block (repeated `count` times):**
```
[instance_idx:u16][start_idx:u16][items:u16][data:type*items]
```
- `instance_idx` (u16): Instance identifier
- `start_idx` (u16): Start index in the array
- `items` (u16): Number of items in this block
- `data`: Array of `items` elements, each `sizeof(type)` bytes

**Total Size:** `3 + count × (6 + items × sizeof(type))` bytes

**Quick Reference:**
```
FILL_ARRAY: [u8 ctx][u8 type][u8 cnt] + cnt×{[u16 inst][u16 start][u16 items] + items×[type]}
```

**Function:** `emu_mem_fill_instance_array()`

---
