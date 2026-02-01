# Emulator Packet Formats Reference

Complete packet structure documentation for the emulator system. Packets are processed in the order listed below.

## Quick Reference - Header Bytes

| Header | Name | Order Code | Description |
|--------|------|------------|-------------|
| `0xF0` | MEM_DECL / CONTEXT_CFG | `0xAAF0` | Memory context creation |
| `0xF1` | MEM_INIT / VARIABLES | `0xAAF1` | Instance creation |
| `0xFA` | SCALAR_DATA | `0xAAFA` | Fill scalar instances with data |
| `0xFB` | ARRAY_DATA | `0xAAFB` | Fill array instances with data |
| `0xA0` | CODE_HDR | - | Code header (block count) |
| `0xAA` | CODE_CFG | `0xAAAA` | Code configuration |
| `0xB0` | BLK_HDR | `0xAAB0` | Block header |
| `0xB1` | BLK_IN | `0xAAB1` | Block input connection |
| `0xB2` | BLK_OUT | `0xAAB2` | Block output connection |
| `0xBA` | BLK_DATA | `0xAABA` | Block custom data |

**Note:** All multi-byte values use **little-endian** byte order (`<` in struct.pack).

---

## Type Reference

```c
typedef enum {
    MEM_U8  = 0x00,  // Size: 1 byte
    MEM_U16 = 0x01,  // Size: 2 bytes
    MEM_U32 = 0x02,  // Size: 4 bytes
    MEM_I16 = 0x03,  // Size: 2 bytes
    MEM_I32 = 0x04,  // Size: 4 bytes
    MEM_B   = 0x05,  // Size: 1 byte (bool)
    MEM_F   = 0x06,  // Size: 4 bytes (float)
} mem_types_t;
```

---

# Part 1: Memory / Variable Packets

## 1. Context Creation

**Order Code:** `ORD_PARSE_CONTEXT_CFG = 0xAAF0`

**Purpose:** Allocate memory contexts for variable storage. Perform only once per context.

**Note:** There are 7 types (TYPES_CNT=7) as of now. Context must be sent in a single packet.

**Packet Structure:**
```
Header (0xF0) + [uint8_t context_id] + TYPES_CNT(7) × (
    [uint32_t heap_elements] + 
    [uint16_t max_instances] + 
    [uint16_t max_dims]
)
```

**Field Descriptions:**
- `context_id` - Context identifier (0-7, must be unique)
  - Context 0 = user variables
  - Context 1+ = block internal outputs
- `heap_elements` - Total unique elements per type (example: 2 scalars + array[10] = 12 elements)
- `max_instances` - Number of instances to allocate (each scalar or array = 1 instance)
- `max_dims` - Total dimension count across all instances (example: scalar + array[1][2] + array[1] = 3 dims)

**Handler Function:**
```c
emu_result_t emu_mem_parse_create_context(const uint8_t *data, const uint16_t data_len, void *nothing);
```

**Example:**
```python
# Create context 0 for user variables
# 7 types, each with specific allocation:
# BOOL: 10 elements, 5 instances, 0 dims (scalars only)
[F0][00][0A 00 00 00][05 00][00 00]  # BOOL
     [00 00 00 00][00 00][00 00]      # (other types...)
```

---

## 2. Instance Creation

**Order Code:** `ORD_PARSE_VARIABLES = 0xAAF1`

**Prerequisites:** Context must be created first

**Purpose:** Define individual variable instances (scalars or arrays) with their properties.

**Packet Structure:**
```
Header (0xF1) + [instance_head_t] + dims_cnt × [uint16_t dims_size]
```
Repeat N times for multiple instances in a single packet.

**Instance Header:**
```c
typedef struct __packed {
    uint16_t context   : 3;  // Context ID (0-7)
    uint16_t dims_cnt  : 4;  // Dimension count (0=scalar, 1+=array)
    uint16_t type      : 4;  // Type (see mem_types_t)
    uint16_t updated   : 1;  // Updated flag (always true initially)
    uint16_t can_clear : 1;  // Can clear flag
    uint16_t reserved  : 3;
} instance_head_t;
```

**Details:**
- `updated` - Set to true on creation
- `can_clear` behavior:
  - `false` → `updated` stays true permanently
  - `true` → blocks can clear `updated` flag during execution
- `type` must exist in the context
- If `dims_cnt > 0`, followed by dimension sizes

**Handler Function:**
```c
emu_result_t emu_mem_parse_instance_packet(const uint8_t *data, const uint16_t data_len, void *nothing);
```

**Examples:**
```python
# Scalar FLOAT variable "speed" at ctx=0, idx=0
[F1][instance_head: ctx=0, dims=0, type=FLOAT, updated=1, can_clear=1]

# Array FLOAT[10] variable "data" at ctx=0, idx=1
[F1][instance_head: ctx=0, dims=1, type=FLOAT, updated=1, can_clear=1][0A 00]  # dim[0]=10

# 2D Array FLOAT[3][4] at ctx=0, idx=2
[F1][instance_head: ctx=0, dims=2, type=FLOAT][03 00][04 00]  # dims: 3×4
```

---

## 3. Instance Data Fill

### 3a. Scalar Data

**Order Code:** `ORD_PARSE_VARIABLES_S_DATA = 0xAAFA`

**Purpose:** Initialize scalar variables with values.

**Packet Structure:**
```
Header (0xFA) + [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + 
    count × ([uint16_t inst_idx] + [data of sizeof(type)])
```

**Details:**
- Uses memcpy for efficient data transfer

**Handler Function:**
```c
emu_result_t emu_mem_fill_instance_scalar(const uint8_t *data, const uint16_t data_len, void *nothing);
```

**Example:**
```python
# Initialize scalar variables in ctx=0
# Set FLOAT at idx=0 to 3.14, and idx=1 to 2.5
[FA][00][06][02]           # ctx=0, type=FLOAT, count=2
    [00 00][C3 F5 48 40]   # idx=0, value=3.14f
    [01 00][00 00 20 40]   # idx=1, value=2.5f

# Initialize BOOL at idx=5 to true
[FA][00][05][01][05 00][01]  # ctx=0, type=BOOL, count=1, idx=5, value=true
```

### 3b. Array Data

**Order Code:** `ORD_PARSE_VARIABLES_ARR_DATA = 0xAAFB`

**Purpose:** Initialize array variables with values. Arrays can be split across multiple packets.

**Packet Structure:**
```
Header (0xFB) + [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + 
    count × (
        [uint16_t instance_idx] + 
        [uint16_t start_idx] + 
        [uint16_t items] + 
        items × [data of sizeof(type)]
    )
```

**Details:**
- Arrays treated as 1D in memory (row-major for multi-dimensional)
- `start_idx` allows splitting large arrays across packets
- Uses memcpy internally for performance

**Handler Function:**
```c
emu_result_t emu_mem_fill_instance_array(const uint8_t *data, const uint16_t data_len, void *nothing);
```

**Example:**
```python
# Fill array[10] at idx=1, elements 0-2 with [1.0, 2.0, 3.0]
[FB][00][06][01]                    # ctx=0, type=FLOAT, count=1
    [01 00]                          # inst_idx=1
    [00 00]                          # start_idx=0
    [03 00]                          # items=3
    [00 00 80 3F][00 00 00 40][00 00 40 40]  # 1.0f, 2.0f, 3.0f

# Continue filling same array, elements 3-5
[FB][00][06][01][01 00][03 00][03 00][...more data...]
```

---

# Part 2: Code / Block Packets

## 4. Loop Configuration

**Order Code:** `ORD_PARSE_LOOP_CFG = 0xAAA0`

**Status:** ⚠️ **Not implemented**

---

## 5. Code Configuration

**Order Code:** `ORD_PARSE_CODE_CFG = 0xAAAA`

**Purpose:** Initialize code execution context and specify block count, or send control commands.

### 5a. Code Header (Block Count)

**Packet Structure:**
```
Header (0xAA) + [uint16_t blocks_count]
```

**Handler Function:**
```c
emu_result_t emu_block_parse_create_list(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Example:**
```python
# Declare 5 blocks in the program
[AA][05 00]  # blocks_count=5
```

### 5b. Control Commands (Alternative use)

**Packet Structure:**
```
Header (0xAA) + [uint8_t order]
```

**Order Values:**
```c
typedef enum {
    ORDER_STOP   = 0,
    ORDER_START  = 1,
    ORDER_STEP   = 2,
    ORDER_PAUSE  = 3,
    ORDER_RESUME = 4,
} order_t;
```

**Example:**
```python
[AA][01]  # START execution
[AA][00]  # STOP execution
```

---

## 6. Memory Access Space Configuration

**Order Code:** `ORD_PARSE_ACCESS_CFG = 0xAAAB`

**Status:** ⚠️ **Not implemented**

---

## 7. Block Header

**Order Code:** `ORD_PARSE_BLOCK_HEADER = 0xAAB0`

**Purpose:** Define block type and I/O port counts.

**Packet Structure:**
```
Header (0xB0) + [cfg]
```

**Configuration Struct:**
```c
struct __packed {
    uint16_t block_idx;         // Block index in code (0-based)
    uint16_t in_connected_mask; // Bitmask of connected inputs
    uint8_t  block_type;        // Block type (see block_types_t)
    uint8_t  in_cnt;            // Input port count
    uint8_t  q_cnt;             // Output port count
} cfg;
```

**Details:**
- Creates block and allocates input/output lists
- Uses memcpy for efficiency (performed by memcpy)
- Code header (block count) must be sent first (block list must be created first)

**Handler Function:**
```c
emu_result_t emu_block_parse_cfg(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Block Types:**
```c
typedef enum {
    BLOCK_MATH     = 0x01,
    BLOCK_SET      = 0x02,
    BLOCK_TIMER    = 0x03,
    BLOCK_COUNTER  = 0x04,
    BLOCK_CLOCK    = 0x05,
    BLOCK_LOGIC    = 0x06,
    BLOCK_FOR      = 0x07,
    BLOCK_SELECTOR = 0x08,
} block_types_t;
```

**Examples:**
```python
# MATH block at idx=0: 3 inputs (EN, IN0, IN1), 2 outputs (ENO, RESULT)
[B0][00 00][06 00][01][03][02]
#    idx    mask   type in  out

# TIMER block at idx=1: 2 inputs, 3 outputs (ENO, Q, ET)
[B0][01 00][03 00][03][02][03]

# COUNTER block at idx=2: 4 inputs, 3 outputs (ENO, Q, CV)
[B0][02 00][0F 00][04][04][03]
```

---

## 8. Block Inputs and Outputs

### 8a. Block Inputs

**Order Code:** `ORD_PARSE_BLOCK_INPUTS = 0xAAB1`

**Packet Structure:**
```
Header (0xB1) + [uint16_t block_idx] + [uint8_t in_idx] + access_node...
```

**Handler Function:**
```c
emu_result_t emu_block_parse_input(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

### 8b. Block Outputs

**Order Code:** `ORD_PARSE_BLOCK_OUTPUTS = 0xAAB2`

**Packet Structure:**
```
Header (0xB2) + [uint16_t block_idx] + [uint8_t q_idx] + access_node...
```

**Handler Function:**
```c
emu_result_t emu_block_parse_output(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Details:** Input and output packets wrap access nodes to specify connections. Logic is identical for both.

**Memory Access Packet Structure (Internal):**

```c
typedef struct __packed {
    uint8_t  type               : 4;  // mem_types_t
    uint8_t  ctx_id             : 3;  // Context ID
    uint8_t  is_index_resolved  : 1;  // Static index flag
    uint8_t  dims_cnt           : 3;  // Dimension count
    uint8_t  idx_type           : 3;  // Index type per dimension
    uint8_t  reserved           : 2; 
    uint16_t instance_idx;            // Instance index
} access_packet;
```

**Access Packet Structure:**
```
access_packet + (if dims_cnt > 0) → idx1[access_packet or uint16_t (check idx_type bit), recursive...] + idx2[...] + ...
```

**Notes (for arrays):**
- Parsing and memory access done recursively if user requested
- Can have nested access packets
- If all indices are static/numbers, then created access node has resolved flat index in array

---

### Access Node Structure


**Note:** Unconnected ports are simply not defined in packets.

**Packet Formats:**

```c
typedef struct __packed {
    uint8_t  type               : 4;  // mem_types_t
    uint8_t  ctx_id             : 3;  // Context ID
    uint8_t  is_index_resolved  : 1;  // Static index flag
    uint8_t  dims_cnt           : 3;  // Dimension count
    uint8_t  idx_type           : 3;  // Index type per dimension
    uint8_t  reserved           : 2; 
    uint16_t instance_idx;            // Instance index
} access_packet;
```

If `dims_cnt > 0`, followed by nested index descriptors (recursive):
- Each index can be static `uint16_t` or dynamic `access_packet`
- `idx_type` bits indicate which (0=static, 1=dynamic)

**Notes:**
- Parsing done recursively for nested array access
- All static indices → resolved flat index at parse time

```
[packed structure][uint16_t / packed structure   index]......
```

**Examples:**
```python
#BLK[0] INPUT[0]# B1 0000 00      8500            0000
#BLK[1] INPUT[0]# B1 0100 00      9500            0000
#BLK[1] INPUT[1]# B1 0100 01      8600            0000
#BLK[1] INPUT[2]# B1 0100 02      8600            0100
#BLK[2] INPUT[0]# B1 0200 00      9500            0100
#-----------------^--Block-^input-^packed stuct - ^ instance idx
```

---

## 9. Block Custom Data

**Order Code:** `ORD_PARSE_BLOCK_DATA = 0xAABA`

**Purpose:** Send block-specific configuration, constants, or instructions.

**Packet Structure:**
```
Header (0xBA) + [uint16_t block_idx] + [uint8_t block_type] + [uint8_t packet_id] + [data...]
```

**Note:** Block parser receives: `[packet_id:u8][data...]`

**Dispatcher Function:**
```c
emu_result_t emu_block_parse_data(const uint8_t *data, const uint16_t data_len, void *emu_code_handle);
```

**Block Parser Signature (must be unified):**
```c
emu_result_t block_<name>_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
```

### Common Packet IDs

```c
typedef enum {
    BLOCK_PKT_CONSTANTS    = 0x00,  // Constants: [cnt:u8][float...]
    BLOCK_PKT_CFG          = 0x01,  // Block-specific config
    BLOCK_PKT_INSTRUCTIONS = 0x10,  // RPN bytecode (Math/Logic)
    BLOCK_PKT_OPTIONS_BASE = 0x20,  // Selector options: 0x20 + n
} block_packet_id_t;
```

---

### Block-Specific Formats

#### MATH Block (0x01)

**Inputs:** EN (bool), IN0..INn (any)  
**Outputs:** ENO (bool), RESULT (float)

**Constants Packet (0x00):**
```
[BA][idx:u16][01][00][count:u8][float × count]
```

**Instructions Packet (0x10):**
```
[BA][idx:u16][01][10][count:u8][instruction × count]
```

**Instruction Format:** `[opcode:u8][operand:u8]`

**Math Opcodes:**
| Opcode | Value | Operand | Description |
|--------|-------|---------|-------------|
| PUSH_CONST | 0x01 | const_idx | Push constant[operand] |
| PUSH_VAR | 0x02 | var_idx | Push input variable[operand] |
| ADD | 0x10 | - | Pop 2, push sum |
| SUB | 0x11 | - | Pop 2, push difference |
| MUL | 0x12 | - | Pop 2, push product |
| DIV | 0x13 | - | Pop 2, push quotient |
| NEG | 0x14 | - | Pop 1, push negation |

**Example - Expression: `a + b * 2.5`**
```python
# RPN: a b 2.5 * +
# Constants packet
[BA][00 00][01][00][01][00 00 20 40]  # const[0]=2.5f

# Instructions packet
[BA][00 00][01][10][05]
    [02 00]  # PUSH_VAR 0 (a)
    [02 01]  # PUSH_VAR 1 (b)
    [01 00]  # PUSH_CONST 0 (2.5)
    [12 00]  # MUL
    [10 00]  # ADD
```

---

#### SET Block (0x02)

**Inputs:** EN (bool), VALUE (any type)  
**Outputs:** ENO (bool)

No custom data packets required.

---

#### TIMER Block (0x03)

**Inputs:** EN (bool), PRESET_IN (any, optional)  
**Outputs:** ENO (bool), Q (bool), ET (float)

**Config Packet (0x01):**
```
[BA][idx:u16][03][01][type:u8][default_pt:u32]
```

**Timer Types:**
- `0` = TON (On-delay)
- `1` = TOF (Off-delay)
- `2` = TP (Pulse)

**Example:**
```python
# TON timer with 5000ms preset
[BA][01 00][03][01][00][88 13 00 00]
```

---

#### COUNTER Block (0x04)

**Inputs:** EN (bool), CU (bool), CD (bool), RESET (bool)  
**Outputs:** ENO (bool), Q (bool), CV (u32)

**Config Packet (0x01):**
```
[BA][idx:u16][04][01][mode:u8][start:f32][step:f32][max:f32][min:f32]
```

**Counter Modes:**
- `0` = CTU (Count up)
- `1` = CTD (Count down)
- `2` = CTUD (Count up/down)

**Example:**
```python
# CTU: start=0, step=1, max=100, min=0
[BA][02 00][04][01][00]
    [00 00 00 00]        # start=0.0f
    [00 00 80 3F]        # step=1.0f
    [00 00 C8 42]        # max=100.0f
    [00 00 00 00]        # min=0.0f
```

---

#### CLOCK Block (0x05)

**Inputs:** EN (bool)  
**Outputs:** ENO (bool), Q (bool)

**Config Packet (0x01):**
```
[BA][idx:u16][05][01][period:u32][width:u32]
```

**Example:**
```python
# Period=1000ms, width=500ms (50% duty cycle)
[BA][03 00][05][01][E8 03 00 00][F4 01 00 00]
```

---

#### LOGIC Block (0x06)

**Inputs:** EN (bool), IN0..INn (bool)  
**Outputs:** ENO (bool), RESULT (bool)

**Constants Packet (0x00):**
```
[BA][idx:u16][06][00][count:u8][bool × count]
```

**Instructions Packet (0x10):**
```
[BA][idx:u16][06][10][count:u8][instruction × count]
```

**Logic Opcodes:**
| Opcode | Value | Description |
|--------|-------|-------------|
| AND | 0x20 | Pop 2, push AND |
| OR | 0x21 | Pop 2, push OR |
| XOR | 0x22 | Pop 2, push XOR |
| NOT | 0x23 | Pop 1, push NOT |
| EQ | 0x30 | Pop 2, push == |
| NE | 0x31 | Pop 2, push != |
| LT | 0x32 | Pop 2, push < |
| LE | 0x33 | Pop 2, push <= |
| GT | 0x34 | Pop 2, push > |
| GE | 0x35 | Pop 2, push >= |

**Example - Expression: `a AND (b OR NOT c)`**
```python
# RPN: a b c NOT OR AND
[BA][04 00][06][10][06]
    [02 00]  # PUSH_VAR 0 (a)
    [02 01]  # PUSH_VAR 1 (b)
    [02 02]  # PUSH_VAR 2 (c)
    [23 00]  # NOT
    [21 00]  # OR
    [20 00]  # AND
```

---

#### FOR Block (0x07)

**Inputs:** EN (bool), START (i32), END (i32), STEP (i32)  
**Outputs:** ENO (bool), INDEX (i32), DONE (bool)

**Constants Packet (0x00):**
```
[BA][idx:u16][07][00][start:f32][end:f32][step:f32]
```

**Config Packet (0x01):**
```
[BA][idx:u16][07][01][chain_len:u16][condition:u8][operator:u8]
```

---

#### SELECTOR Block (0x08)

**Inputs:** EN (bool), SELECT (u8)  
**Outputs:** ENO (bool), OUT (float)

**Option Packets (0x20 + n):**
```
[BA][idx:u16][08][0x20+n][access_node...]
```

Each option is a separate packet with `packet_id = 0x20 + option_index`.

**Example:**
```python
# Option 0: constant 1.0
[BA][05 00][08][20][01][06][00 00 80 3F]

# Option 1: variable at ctx=0, idx=0
[BA][05 00][08][21][02][00][00 00][06]

# Option 2: block 0 output port 1
[BA][05 00][08][22][03][00 00][01]
```

---

## Packet Generation Order

Complete sequence for a typical program:

1. **Memory Packets** (in order):
   - `MEM_DECL (0xF0)` - Declare all contexts
   - `MEM_INIT (0xF1)` - Define all instances
   - `SCALAR_DATA (0xFA)` - Initialize scalar values
   - `ARRAY_DATA (0xFB)` - Initialize array values

2. **Code Header:**
   - `CODE_HDR (0xAA)` - Specify block count

3. **Block Packets** (repeat for each block):
   - `BLK_HDR (0xB0)` - Block header
   - `BLK_IN (0xB1)` × N - Input connections
   - `BLK_OUT (0xB2)` × M - Output connections
   - `BLK_DATA (0xBA)` × K - Block-specific config

4. **Start Execution:**
   - `CODE_CFG (0xAA)` - Send START command

---

## Complete Example Session

```python
# 1. Declare context 0 (simplified - only FLOAT type shown)
[F0][00][0C 00 00 00][03 00][00 00]  # 12 elements, 3 instances, 0 dims

# 2. Define instances
[F1][head: ctx=0, dims=0, type=FLOAT]  # idx=0: "a"
[F1][head: ctx=0, dims=0, type=FLOAT]  # idx=1: "b"
[F1][head: ctx=0, dims=0, type=FLOAT]  # idx=2: "result"

# 3. Initialize scalars
[FA][00][06][02]
    [00 00][00 00 80 3F]  # a = 1.0
    [01 00][00 00 00 40]  # b = 2.0

# 4. Code header
[AA][01 00]  # 1 block

# 5. Block header: MATH
[B0][00 00][03 00][01][03][02]  # idx=0, type=MATH, 3 in, 2 out

# 6. Block inputs
[B1][00 00][00][01][05][01]              # IN0: const TRUE
[B1][00 00][01][02][00][00 00][06]       # IN1: var "a"
[B1][00 00][02][02][00][01 00][06]       # IN2: var "b"

# 7. Block outputs
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



