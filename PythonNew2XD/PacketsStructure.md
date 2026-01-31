# Packet Structure Reference

## Header Bytes
| Header | Name | Description |
|--------|------|-------------|
| `0xF0` | MEM_DECL | Memory declaration |
| `0xF1` | MEM_INIT | Memory initialization |
| `0xFB` | MEM_DUMP | Memory dump request |
| `0xA0` | CODE_HDR | Code header |
| `0xAA` | CODE_CFG | Code config |
| `0xB0` | BLK_HDR | Block header |
| `0xB1` | BLK_IN | Block input |
| `0xB2` | BLK_OUT | Block output |
| `0xBA` | BLK_DATA | Block data |

## Memory Packets
```
MEM_DECL (0xF0): [F0][ctx:u8][idx:u16][type:u8][count:u16]
MEM_INIT (0xF1): [F1][ctx:u8][idx:u16][type:u8][value:N bytes]
MEM_DUMP (0xFB): [FB][ctx:u8][idx:u16][type:u8]
```
- `ctx`: 0=user, 1=blocks | `type`: see Type Reference | value size depends on type

## Code Packets
```
CODE_HDR (0xA0): [A0][block_count:u16]
CODE_CFG (0xAA): [AA][order:u8]
```
- `order`: 0=STOP, 1=START, 2=STEP, 3=PAUSE, 4=RESUME

## Block Packets
```
BLK_HDR (0xB0): [B0][idx:u16][type:u8][in_cnt:u8][out_cnt:u8]
BLK_IN  (0xB1): [B1][idx:u16][port:u8][access_node...]
BLK_OUT (0xB2): [B2][idx:u16][port:u8][access_node...]
BLK_DATA(0xBA): [BA][idx:u16][type:u8][pkt_id:u8][data...]
```

## Access Node Structure
```
[node_type:u8][payload...]
```
| Type | Payload | Description |
|------|---------|-------------|
| `0x00` NONE | - | No connection |
| `0x01` CONST | `[mem_type:u8][value:N]` | Constant value |
| `0x02` VAR | `[ctx:u8][idx:u16][mem_type:u8]` | Variable reference |
| `0x03` BLOCK | `[blk_idx:u16][port:u8]` | Block output reference |

## Block Types (`blck_types_t`)
| Value | Type |
|-------|------|
| 0x01 | MATH |
| 0x02 | SET |
| 0x03 | TIMER |
| 0x04 | COUNTER |
| 0x05 | CLOCK |
| 0x06 | LOGIC |
| 0x07 | FOR |
| 0x08 | SELECTOR |

## Packet IDs (`block_packet_id_t`)
| Value | ID | Usage |
|-------|-----|-------|
| 0x00 | PKT_CONSTANTS | Constants: [cnt:u8][float...] |
| 0x01 | PKT_CFG | Block configuration |
| 0x10 | PKT_INSTRUCTIONS | Math/Logic RPN bytecode |
| 0x20+ | PKT_OPTIONS_BASE | Selector options (0x20+n) |

## Block-Specific Data Formats

### MATH (0x01)
```
PKT_CONSTANTS (0x00): [BA][idx:u16][01][00][count:u8][f32...]
PKT_INSTRUCTIONS(0x10): [BA][idx:u16][01][10][count:u8][instr...]
```
Instruction: `[opcode:u8][operand:u8]` | Outputs: ENO(bool), RESULT(float)

### SET (0x02)
No BLK_DATA packets | Outputs: ENO(bool)

### TIMER (0x03)
```
PKT_CFG (0x01): [BA][idx:u16][03][01][type:u8][default_pt:u32]
```
Types: TON=0, TOF=1, TP=2 | Outputs: ENO(bool), Q(bool), ET(float)

### COUNTER (0x04)
```
PKT_CFG (0x01): [BA][idx:u16][04][01][mode:u8][start:f32][step:f32][max:f32][min:f32]
```
Types: CTU=0, CTD=1, CTUD=2 | Outputs: ENO(bool), Q(bool), CV(u32)

### CLOCK (0x05)
```
PKT_CFG (0x01): [BA][idx:u16][05][01][period:f32][width:f32]
```
Outputs: ENO(bool), Q(bool)

### LOGIC (0x06)
```
PKT_CONSTANTS (0x00): [BA][idx:u16][06][00][count:u8][u8...]
PKT_INSTRUCTIONS(0x10): [BA][idx:u16][06][10][count:u8][instr...]
```
Outputs: ENO(bool), RESULT(bool)

### FOR (0x08)
```
PKT_CONSTANTS (0x00): [BA][idx:u16][08][00][start:f32][end:f32][step:f32]
PKT_CFG (0x01): [BA][idx:u16][08][01][chain_len:u16][condition:u8][operator:u8]
```
Outputs: ENO(bool), INDEX(f32), DONE(bool)

### SELECTOR (0x0A)
```
PKT_OPTIONS_n (0x20+n): [BA][idx:u16][0A][2n][access_node...]
```
Outputs: ENO(bool), OUT(float)

## Type Reference (`mem_types_t`)
| Value | Type | Size |
|-------|------|------|
| 0x00 | U8 | 1 |
| 0x01 | U16 | 2 |
| 0x02 | U32 | 4 |
| 0x03 | I16 | 2 |
| 0x04 | I32 | 4 |
| 0x05 | B (bool) | 1 |
| 0x06 | F (float) | 4 |

## Math/Logic Opcodes
| Op | Code | Op | Code |
|----|------|----|------|
| PUSH_CONST | 0x01 | PUSH_VAR | 0x02 |
| ADD | 0x10 | SUB | 0x11 |
| MUL | 0x12 | DIV | 0x13 |
| NEG | 0x14 | AND | 0x20 |
| OR | 0x21 | XOR | 0x22 |
| NOT | 0x23 | EQ | 0x30 |
| NE | 0x31 | LT | 0x32 |
| LE | 0x33 | GT | 0x34 |
| GE | 0x35 | | |

## Packet Generation Order
1. `MEM_DECL` - Declare all variables
2. `MEM_INIT` - Initialize variables with values
3. `CODE_HDR` - Code header with block count
4. Per block:
   - `BLK_HDR` - Block header
   - `BLK_IN` × N - Input connections
   - `BLK_OUT` × N - Output connections
   - `BLK_DATA` × N - Block-specific config
5. `CODE_CFG` - Order command (START/STOP/etc)

## Notes
- All multi-byte values: **little-endian** (`<` in struct.pack)
- Block index (`idx`): 0-based, max 65535
- Context 0 = user variables, Context 1 = block internal outputs
- Access nodes are variable-length based on node_type
- Selector options indexed from 0x10 (option 0 = 0x10, option 1 = 0x11, etc.)

---

## Packet Examples

### Memory Declaration & Initialization
```python
# Declare: var "speed" as FLOAT at ctx=0, idx=0
[F0][00][00 00][08][01 00]  # MEM_DECL ctx=0, idx=0, type=FLOAT, count=1

# Initialize: speed = 3.14
[F1][00][00 00][08][C3 F5 48 40]  # MEM_INIT ctx=0, idx=0, type=FLOAT, value=3.14f
```

### Block Header Examples
```python
# MATH block at index 0: 2 inputs (EN, IN0), 2 outputs (ENO, RESULT)
[B0][00 00][01][02][02]  # BLK_HDR idx=0, type=MATH, in=2, out=2

# TIMER block at index 1: 2 inputs (EN, PRESET_IN), 3 outputs (ENO, Q, ET)
[B0][01 00][03][02][03]  # BLK_HDR idx=1, type=TIMER, in=2, out=3

# COUNTER block at index 2: 4 inputs (EN, CU, CD, RESET), 3 outputs (ENO, Q, CV)
[B0][02 00][04][04][03]  # BLK_HDR idx=2, type=COUNTER, in=4, out=3
```

### Block Input Examples
```python
# Input port 0 connected to constant TRUE (bool)
[B1][00 00][00][01][01][01]  # BLK_IN idx=0, port=0, CONST, BOOL, value=1

# Input port 1 connected to variable "speed" (ctx=0, idx=0, FLOAT)
[B1][00 00][01][02][00][00 00][08]  # BLK_IN idx=0, port=1, VAR, ctx=0, idx=0, FLOAT

# Input port 0 connected to block 0 output port 1
[B1][01 00][00][03][00 00][01]  # BLK_IN idx=1, port=0, BLOCK, blk=0, port=1
```

### Block Output Examples
```python
# Output port 1 (RESULT) connected to variable "result" (ctx=0, idx=1)
[B2][00 00][01][02][00][01 00][08]  # BLK_OUT idx=0, port=1, VAR, ctx=0, idx=1, FLOAT

# Output port 0 (ENO) not connected
[B2][00 00][00][00]  # BLK_OUT idx=0, port=0, NONE
```

### Block Data Examples

#### MATH Block - Expression: `a + b * 2.5`
```python
# Constants packet: [2.5]
[BA][00 00][01][00][01][00 00 20 40]  # BLK_DATA MATH, PKT_CONSTANTS, count=1, 2.5f

# Instructions packet: PUSH_VAR(0), PUSH_VAR(1), PUSH_CONST(0), MUL, ADD
[BA][00 00][01][10][05][02 00][02 01][01 00][12 00][10 00]
# 5 instructions: push var0, push var1, push const0, mul, add
```

#### TIMER Block - TON, 5000ms preset
```python
[BA][01 00][03][01][00][88 13 00 00]  # BLK_DATA TIMER, PKT_CFG, type=TON, default_pt=5000
```

#### COUNTER Block - CTU, start=0, step=1, max=100, min=0
```python
[BA][02 00][04][01][00][00 00 00 00][00 00 80 3F][00 00 C8 42][00 00 00 00]
# BLK_DATA COUNTER, PKT_CFG, mode=0, start=0.0, step=1.0, max=100.0, min=0.0
```

#### CLOCK Block - period=1000ms, width=500ms
```python
[BA][03 00][05][01][00 00 7A 44][00 00 FA 43]  # BLK_DATA CLOCK, PKT_CFG, period=1000.0f, width=500.0f
```

#### LOGIC Block - Expression: `a AND (b OR NOT c)`
```python
# Constants packet (bools): [0, 1] for false/true if needed
[BA][04 00][06][00][02][00][01]  # count=2, false, true

# Instructions: PUSH_VAR(a), PUSH_VAR(b), PUSH_VAR(c), NOT, OR, AND
[BA][04 00][06][10][06][02 00][02 01][02 02][23 00][21 00][20 00]
```

#### SELECTOR Block - 3 options
```python
# Option 0: constant 1.0
[BA][05 00][0A][20][01][08][00 00 80 3F]  # pkt_id=0x20, CONST FLOAT 1.0

# Option 1: variable at ctx=0, idx=0
[BA][05 00][0A][21][02][00][00 00][08]  # pkt_id=0x21, VAR ctx=0 idx=0 FLOAT

# Option 2: block 0 output 1
[BA][05 00][08][12][03][00 00][01]  # pkt_id=0x12, BLOCK blk=0 port=1
```

### Complete Code Session Example
```
1.  [F0][00][00 00][08][01 00]     # Declare "a" as FLOAT
2.  [F0][00][01 00][08][01 00]     # Declare "b" as FLOAT
3.  [F0][00][02 00][08][01 00]     # Declare "result" as FLOAT
4.  [F1][00][00 00][08][00 00 80 3F]  # Init a = 1.0
5.  [F1][00][01 00][08][00 00 00 40]  # Init b = 2.0
6.  [A0][01 00]                    # CODE_HDR: 1 block
7.  [B0][00 00][01][02][02]        # BLK_HDR: MATH block, 2in/2out
8.  [B1][00 00][00][01][01][01]    # BLK_IN: port0 = const TRUE
9.  [B1][00 00][01][02][00][00 00][08]  # BLK_IN: port1 = var "a"
10. [B2][00 00][00][00]            # BLK_OUT: port0 (ENO) = NONE
11. [B2][00 00][01][02][00][02 00][08]  # BLK_OUT: port1 = var "result"
12. [BA][00 00][01][00][01][00 00 00 40]  # BLK_DATA: const [2.0]
13. [BA][00 00][01][10][03][02 00][01 00][10 00]  # BLK_DATA: a + 2.0
14. [AA][01]                       # CODE_CFG: START
```
Result: `result = a + 2.0 = 1.0 + 2.0 = 3.0`

---

## Block I/O Port Reference

### MATH Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1+ | IN | IN0..N | FLOAT |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | RESULT | FLOAT |

### SET Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1 | IN | VALUE | any |
| 0 | OUT | ENO | BOOL |

### TIMER Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1 | IN | PRESET_IN | FLOAT (optional) |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | Q | BOOL |
| 2 | OUT | ET | FLOAT |

### COUNTER Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1 | IN | CU | BOOL |
| 2 | IN | CD | BOOL |
| 3 | IN | RESET | BOOL |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | Q | BOOL |
| 2 | OUT | CV | UINT32 |

### CLOCK Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | Q | BOOL |

### LOGIC Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1+ | IN | IN0..N | BOOL |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | RESULT | BOOL |

### FOR Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1 | IN | START | INT32 |
| 2 | IN | END | INT32 |
| 3 | IN | STEP | INT32 |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | INDEX | INT32 |
| 2 | OUT | DONE | BOOL |

### SELECTOR Block
| Port | Dir | Name | Type |
|------|-----|------|------|
| 0 | IN | EN | BOOL |
| 1 | IN | SELECT | UINT8 |
| 0 | OUT | ENO | BOOL |
| 1 | OUT | OUT | FLOAT |

---

## RPN Expression Parsing

### Math Expression: `(a + b) * c - 2.5`
**Infix → Postfix (RPN):** `a b + c * 2.5 -`

**Generated Instructions:**
| # | Opcode | Operand | Description |
|---|--------|---------|-------------|
| 0 | PUSH_VAR (0x02) | 0 | Push input var 0 (a) |
| 1 | PUSH_VAR (0x02) | 1 | Push input var 1 (b) |
| 2 | ADD (0x10) | 0 | Pop 2, push sum |
| 3 | PUSH_VAR (0x02) | 2 | Push input var 2 (c) |
| 4 | MUL (0x12) | 0 | Pop 2, push product |
| 5 | PUSH_CONST (0x01) | 0 | Push constant[0] = 2.5 |
| 6 | SUB (0x11) | 0 | Pop 2, push difference |

### Logic Expression: `(a AND b) OR (NOT c)`
**Infix → Postfix (RPN):** `a b AND c NOT OR`

**Generated Instructions:**
| # | Opcode | Operand | Description |
|---|--------|---------|-------------|
| 0 | PUSH_VAR (0x02) | 0 | Push input var 0 (a) |
| 1 | PUSH_VAR (0x02) | 1 | Push input var 1 (b) |
| 2 | AND (0x20) | 0 | Pop 2, push AND result |
| 3 | PUSH_VAR (0x02) | 2 | Push input var 2 (c) |
| 4 | NOT (0x23) | 0 | Pop 1, push NOT result |
| 5 | OR (0x21) | 0 | Pop 2, push OR result |
