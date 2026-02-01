# Packet Formats Reference

Packets used in emulator and their structure in order of packet flow.

## 1. Context Creation

**Order Code:** `ORD_PARSE_CONTEXT_CFG = 0xAAF0` (perform only once per instance packet)

**Packet Structure:**
```
Header (0xF0) + [uint8_t context_id] + TYPES_CNT(7) × ([uint32_t heap_elements] + [uint16_t max_instances] + [uint16_t max_dims])
```

**Details:**
- `context_id` must be in range 0-7 and unique
- There are 7 types count as for now, otherwise must send more

**Field Descriptions:**
- `uint32_t heap_elements` - count of unique elements of each type (example: we have 2 scalars and array[10], then there are 12 elements)
- `uint16_t max_instances` - instance count that space shall be created (one scalar is one instance, one array is also one instance)
- `uint16_t max_dims` - total dimensions count that exist in every instance of given type (example: we have 2 scalars array1[1][2], array2[1], total dimensions count is 3)

**Handler Function:**
```c
emu_result_t emu_mem_parse_create_context(const uint8_t *data, const uint16_t data_len, void *nothing);
```

---

## 2. Instance Creation

**Order Code:** `ORD_PARSE_VARIABLES = 0xAAF1`

**Prerequisites:** Perform only when context is created

**Packet Structure:**
```
Header (0xF1) + [instance_head_t] + dims_cnt × [uint16_t dims_size]
```
Repeat N times for multiple instances.

**Instance Header:**
```c
typedef struct __packed {
    uint16_t context   : 3; 
    uint16_t dims_cnt  : 4;
    uint16_t type      : 4;
    uint16_t updated   : 1;
    uint16_t can_clear : 1;
    uint16_t reserved  : 3;
} instance_head_t;
```

**Details:**
- Context must be created before this
- `updated` is set to true anyway
- `type` must exist in context
- `can_clear` is preserved: when `can_clear` is false then `updated` always true, else `updated` can be cleared in code loop by block

**Handler Function:**
```c
emu_result_t emu_mem_parse_instance_packet(const uint8_t *data, const uint16_t data_len, void *nothing);
```

---

## 3. Instance Data Fill

### Scalars

**Order Code:** `ORD_PARSE_VARIABLES_S_DATA = 0xAAFA`

**Packet Structure:**
```
Header (0xFA) + [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + count × ([uint16_t inst_idx] + [data of sizeof(type)])
```

**Handler Function:**
```c
emu_result_t emu_mem_fill_instance_scalar(const uint8_t *data, const uint16_t data_len, void *nothing);
```

### Arrays

**Order Code:** `ORD_PARSE_VARIABLES_ARR_DATA = 0xAAFB`

**Packet Structure:**
```
Header (0xFB) + [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + 
    count × ([uint16_t instance_idx] + [uint16_t start_idx] + [uint16_t items] + items × [data of sizeof(type)])
```

**Details:**
- In parse memcpy is used
- Arrays can be split if too large (using start_idx)
- Arrays in memory are treated as 1D array

**Handler Function:**
```c
emu_result_t emu_mem_fill_instance_array(const uint8_t *data, const uint16_t data_len, void *nothing);
```

---

## 4. Loop Configuration

**Order Code:** `ORD_PARSE_LOOP_CFG = 0xAAA0`

**Status:** ⚠️ **Not implemented**

---

## 5. Code Configuration

**Order Code:** `ORD_PARSE_CODE_CFG = 0xAAAA`

**Packet Structure:**
```
Header (0xAA) + [uint16_t blocks_count] + ...
```

**Details:** Creates list of blocks

**Handler Function:**
```c
emu_result_t emu_block_parse_create_list(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

---

## 6. Memory Access Space Configuration

**Order Code:** `ORD_PARSE_ACCESS_CFG = 0xAAAB`

**Status:** ⚠️ **Not implemented**

---

## 7. Block Configuration Creation

**Order Code:** `ORD_PARSE_BLOCK_HEADER = 0xAAB0`

**Packet Structure:**
```
Header (0xB0) + [cfg]
```

**Configuration Struct:**
```c
struct __packed {
    uint16_t block_idx;         // index of block in code
    uint16_t in_connected_mask; // connected inputs (those that have instance)
    uint8_t block_type;         // type of block (function)
    uint8_t in_cnt;             // count of inputs
    uint8_t q_cnt;              // count of outputs
} cfg;
```

**Details:**
- Create block and list of inputs and outputs
- Performed by memcpy
- Block list must be created first

**Handler Function:**
```c
emu_result_t emu_block_parse_cfg(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

---

## 8. Block Inputs and Outputs

### Inputs

**Order Code:** `ORD_PARSE_BLOCK_INPUTS = 0xAAB1`

**Packet Structure:**
```
Header (0xB1) + [uint16_t block_idx] + [uint8_t in_idx] + mem_access_t packet
```

**Handler Function:**
```c
emu_result_t emu_block_parse_input(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

### Outputs

**Order Code:** `ORD_PARSE_BLOCK_OUTPUTS = 0xAAB2`

**Packet Structure:**
```
Header (0xB2) + [uint16_t block_idx] + [uint8_t q_idx] + mem_access_t packet
```

**Handler Function:**
```c
emu_result_t emu_block_parse_output(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Details:** These wrap mem_access_t packets to identify target; functions are identical in terms of logic.

### Memory Access Packet (Annotation)

```c
typedef struct __packed {
    uint8_t  type               : 4;
    uint8_t  ctx_id             : 3;
    uint8_t  is_index_resolved  : 1;
    uint8_t  dims_cnt           : 3;
    uint8_t  idx_type           : 3;
    uint8_t  reserved           : 2; 
    uint16_t instance_idx;
} access_packet;
```

**Access Packet Structure:**
```
access_packet + (if dims_cnt > 0) → idx1[access_packet or uint16_t (check idx_type bit), recursive...] + idx2[...] + ...
```

**Notes (for arrays mostly):**
- Parsing and memory access is done recursively if user requested
- There can be few access packets nested
- If all indices are static/numbers, then created access node has resolved index in array

---

## 9. Block Custom Data Packet

**Order Code:** `ORD_PARSE_BLOCK_DATA = 0xAABA`

**Packet Structure:**
```
Header (0xBA) + [uint16_t block_idx] + [uint8_t block_type] + custom_packet[packet_id:u8][data...]
```

**Note:** Parser receives: `[packet_id:u8][data...]`

**Dispatcher Function (parsers manager):**
```c
emu_result_t emu_block_parse_data(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Block Parser Signature (must be unified):**
```c
emu_result_t block_##name##_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
```

### Common Packet ID Values

**Note:** Try to use `block_packet_id_t`, but it's totally up to block creator.

```c
typedef enum {
    // Data packets (0x00-0x0F)
    BLOCK_PKT_CONSTANTS      = 0x00,  // Constants: [cnt:u8][float...]
    BLOCK_PKT_CFG            = 0x01,  // Configuration: block-specific config data
    
    // Code/expression packets (0x10-0x1F)
    BLOCK_PKT_INSTRUCTIONS   = 0x10,  // Instructions: bytecode for Math, Logic blocks
    
    // Dynamic option packets (0x20+)
    BLOCK_PKT_OPTIONS_BASE   = 0x20,  // Selector options: 0x20 + option_index
} block_packet_id_t;
```


