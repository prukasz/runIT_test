# Virtual Machine (VM)

IMPORTANT: If you modify the source files in this component (`core/**/*.h`, `core/**/*.c`), you must update this documentation to reflect the changes.

## Overview

`VM` is the runtime for runIT's block-based scripting language. A program is compiled off-device into a flat description, uploaded wirelessly, and rebuilt here into a graph of **objects** (the data) wired to **blocks** (the behaviour), executed cyclically by a supervisor.

Three properties shape every decision in this component:

* **No dynamic allocation after load.** The pool is heap-allocated once per upload, sized by the program itself; from then until the next upload every allocation is a bump within it. Individual objects are never freed; replacing a program resets the whole arena and rebuilds. This removes lifetime tracking, fragmentation and the allocation-failure path from the runtime entirely.
* **Structure is fixed at load; only values change at runtime.** Nothing — not a remote write, not a block — can alter an object's type, size, flags or name after it is created. Reshaping the graph means uploading a new program.
* **Everything from the wire is untrusted.** Ids, counts, offsets and lengths arriving in an upload packet are validated before they reach storage, and the whole load path returns `err_h` chains rather than sentinels.

### Directory Map
```
components/VM/
├── CMakeLists.txt
├── VM.MD                 # This documentation
├── blocks/                     # THE PALETTE — one .c per block type
│   ├── vm_blocks.h             # type ids + the placeholders' shared private state
│   ├── vm_blocks_dummy.c       # seven placeholders, one per block shape
│   ├── vm_block_expr.h         # RPN bytecode: layout, both opcode palettes, fault reasons
│   ├── vm_block_expr.c         # EXPR (float) and EXPR_BIT (uint32) — one machine, two words
│   └── vm_blocks_table.c       # g_vm_blocks[] — the palette, entire
└── core/
    ├── errors/
    │   └── sys_error_vm.h      # VM owner map + error tag map + _OWNED error macros
    ├── store/
    │   └── vm_store.h/.c       # THE storage API — per-load pool, bump arena, four id registries
    ├── obj/
    │   ├── vm_obj.h            # vm_obj_head_t / vm_obj_t / vm_obj_h, type widths, header helpers
    │   ├── vm_obj_build.h/.c   # vm_obj_create() / vm_accessor_create()
    │   ├── vm_obj_dyn.h/.c     # heap objects, refcounted, register + teardown
    │   └── vm_obj_access.h/.c  # accessors, payload resolution, typed get/set macros
    ├── block/
    │   ├── vm_block.h          # vm_block_data_t, pin access, EN/ENO, cfg.rt, spans, BLOCK_CALL
    │   ├── vm_block_build.h/.c # vm_block_create(), wiring resolution
    │   ├── vm_block_run.c      # what a block body reaches for: ask, claim a span
    │   └── vm_block_template.h # worked templates: update-driven, enable-driven, span owner
    ├── exec/
    │   ├── vm_section.h/.c     # sections as [start, end) ranges over the block order
    │   ├── vm_event.h/.c       # routed sys_callbacks events — two buffers, swapped per pass
    │   └── vm_exec.h/.c        # the supervisor: pass loop, dispatch, spans, block watchdog
    └── loader/
        └── vm_loader.h/.c      # load state machine, wire-facing validation
```

The groups are stacked bottom-up: `errors` and `store` are depended on by everything, `obj` builds on `store`, `block` on `obj`, `exec` on `block`, and `loader` drives all of them.

`blocks/` sits outside `core/` and depends on it, never the reverse. The supervisor dispatches through the *declaration* of `g_vm_blocks` in `vm_exec.h` and never learns what is in it — which is what keeps `core/` free of `sys_io`, `sys_actions` and every other driver a real block will pull in.

**Every include across the project is by bare filename** — `#include "vm_obj.h"`, never `"obj/vm_obj.h"`. Each group is on the include path instead (see `CMakeLists.txt`), so this tree is a reading aid and regrouping it again later touches no source file. The one path that is spelled out is `../VM/core/errors` in `sys_errors/CMakeLists.txt`, which reaches the error map without a circular `REQUIRES`.

The upload decoder table (`dec_vm_loader.h`, class `0x04`) lives in `components/codecs/decoders/` — see [[CODECS.MD]].

---

## Object Model

An object is a fixed 4-byte header followed by a flexible payload:

```c
typedef struct __packed vm_obj_head_t {
  uint16_t payload_size;      // value length in bytes
  struct { uint8_t obj_t : 4; uint8_t name_size : 4; } d;
  struct {
    uint8_t mutable : 1;      // anything may write it
    uint8_t usr_mutable : 1;  // user logic / remote writes may write it
    uint8_t upd : 1;          // value changed since last read by telemetry
    uint8_t upd_resetable : 1;
    uint8_t tagged : 1;       // name present
    uint8_t retentive : 1;    // persist to NVS
  } f;
} vm_obj_head_t;              // _Static_assert'd at 4 bytes

typedef struct __packed vm_obj_t { vm_obj_head_t head; uint8_t payload[]; } vm_obj_t;
typedef vm_obj_t* vm_obj_h;   // opaque handle, same convention as err_h
```

**The name lives at the tail**, at `payload[payload_size]` onward — not as a prefix. This is why `vm_obj_value()`/`vm_obj_payload_size()` need no branch on `tagged`: the value always starts at `payload[0]` and always runs `payload_size` bytes, whether the object is named or not. Only cold paths (`vm_obj_tag()`, NVS keying, telemetry decode) ever touch the name. `name_size` living in the header rather than as a length byte in the tail means the tag needs no length prefix at all.

Total footprint is `sizeof(head) + payload_size + name_size` (`vm_obj_total_size()`), which is also the stride an arena walker uses.

### Types and widths

```c
VM_OBJ_NONE, VM_OBJ_PTR, VM_OBJ_U8, VM_OBJ_U32, VM_OBJ_I32,
VM_OBJ_F, VM_OBJ_B, VM_OBJ_STR, VM_OBJ_U64
```

Two parallel tables in `vm_obj.h`: `vm_obj_type_sizes[]` (bytes) and `vm_obj_type_shifts[]` (log2 of the same). **Every width is a power of two**, so "how many items fit in `payload_size` bytes" is a shift, not a divide — the single hottest operation in accessor resolution, and one the compiler cannot strength-reduce itself because the width comes from a runtime lookup. Adding a non-power-of-two type would silently truncate; it needs a matching shift entry.

`VM_OBJ_STR` is **an array of chars**, not a distinct string abstraction: one byte per element, no terminator implied by the type. A whole-object accessor gives the buffer, an indexed one gives a single character, and everything that works on a `VM_OBJ_U8` array works here. It stays a separate type from `VM_OBJ_U8` because the type is what tells a client how to render the bytes — which is also why `vm_obj_copy_content()` refuses `STR` → `U8` despite the identical width.

**A new type needs an arm in every value-path switch**, not just the two tables: `vm_payload_read()`, `VM_VAL_CAST_TO()`, `store_scalar()` and `payload_as_index()`. A missing case is not a compile error — it falls to `default` and reads or writes **0**, which is indistinguishable from a genuinely empty value. `VM_OBJ_STR` shipped that way until stage J caught it.

### Nested objects

There is no composite type. A parent is a `VM_OBJ_PTR` array whose elements are `vm_obj_h` values pointing at children — the same shape Python lists, Java jagged arrays and cJSON use. Children are independently typed, tagged, flagged and addressable.

Consequences worth knowing:
* **Pointer payloads are never portable.** They are addresses in this boot's arena. `retentive` is rejected outright for `VM_OBJ_PTR` at creation, `vm_obj_copy_content()` refuses PTR on either side, and the upload protocol carries **child ids**, never addresses.
* Building is order-dependent: children first, then the parent, then link.

---

## Arena (inside `vm_store.c`)

`vm_alloc_t` — `base` / `capacity` / `offset` — is the bump region the pool is carved out of. Chunks are handed out sequentially at 4-byte alignment and **never freed piecemeal**: a program is sized once, built once, and replaced whole, so there is no per-object free and no need for one. The arena is re-armed at each new program's declared size rather than reset in place. Exhaustion emits `ERR_VM_ALLOC_EXHAUSTED` with requested-vs-remaining and returns NULL.

This was its own `vm_alloc.h`/`.c` pair until [vm_store.h](core/store/vm_store.h) became the only caller — a header, a source file and a public API guarding a boundary that had nothing on the other side of it. Carving is now `static` inside `vm_store.c`; the struct stays in the header only because `vm_store_t` embeds it and `vm_store_get()` is inline.

**Alignment note:** a payload begins at `header + 4`, so a `VM_OBJ_U64` element is only 4-byte aligned. Casting to `uint64_t*` would assert 8-byte alignment to the compiler, which on Xtensa can fault or read the wrong words — so 64-bit reads and writes go through `memcpy`, which compiles to the same two word accesses without the claim.

---

## Accessors (`vm_obj_access.h`)

### Object registry

`vm_obj_by_id()` maps id → `vm_obj_h` for the one loaded program, over the shared registry in [vm_store.h](core/store/vm_store.h). It is bounds-checked and returns NULL past the end, so an empty registry (between teardown and load) fails closed everywhere. Accessors and blocks have identical registries — see *One store, three registries*.

### Addressing

```c
typedef struct { vm_obj_t_e type; void* ptr; uint16_t count; } vm_payload_t;

typedef enum { VM_IDX_LITERAL, VM_IDX_REF, VM_IDX_NAME } vm_index_kind_e;

struct vm_accessor_t {
  uint16_t id;                // root object
  uint8_t count;              // chained indices; 0 = whole object
  uint8_t flags;              // VM_ACC_F_CACHED
  const vm_index_t* indices;  // allocated in the same chunk, right after the header
  vm_obj_h c_owner;           // resolution cache, filled by vm_accessor_cache_build()
  void* c_ptr;
  uint16_t c_count;
  uint8_t c_type;
  uint8_t c_pad;
};                            // 20 bytes + 8 per index
```

An accessor is a root id plus a chain of index steps. Each step is:

| kind | meaning | use |
| :--- | :--- | :--- |
| `VM_IDX_LITERAL` | fixed position | compiled wiring |
| `VM_IDX_REF` | position read live from another accessor | selector wires, sequencer step counters |
| `VM_IDX_NAME` | the child whose tag matches | data whose field *order* isn't known at compile time |

`VM_IDX_NAME` requires a `VM_OBJ_PTR` parent with tagged children, and costs a scan with a compare per child — right for the handful of fields in a message, wrong for a compiled path where the position is already known. A miss raises `ERR_VM_ACCESSOR_NAME_NOT_FOUND` carrying the name, which is a routine condition for variable-shape data rather than necessarily a bug.

**A chain step landing on a `VM_OBJ_PTR` element yields the pointer slot, not the object behind it.** Reading a scalar field of a nested object therefore takes *two* steps:

```c
// msg.temp  ->  ["temp"] descends into the child, [0] reads its value
vm_index_t idx[] = {{.kind = VM_IDX_NAME, .name = "temp"},
                    {.kind = VM_IDX_LITERAL, .value = 0}};
```

Stopping after `["temp"]` leaves the payload typed `VM_OBJ_PTR`, which `vm_payload_read()` has no case for — so `VM_OBJ_GET_VAL` yields **0**, silently. This is deliberate and not auto-dereferenced, because `vm_obj_link()` needs the raw slot; a trailing auto-deref would break linking and the switch/demux pattern with it.

`vm_get_obj()` is the exception that *does* follow a trailing `VM_OBJ_PTR` element — but only when there is a chain. A count-0 accessor names the object itself, so a pointer container comes back as the container; following its first pointer would hand a relay or encoder block `child[0]` instead of the object it was wired to, with nothing to indicate the substitution.

`VM_ACCESSOR_MAX_DEPTH` (8) caps by-ref recursion — an uploaded tree is not trusted to be acyclic.

### Accessor table and sharing

Accessors have their own id space (`vm_accessor_by_id()`), built the same way objects are. **Sharing is the expected case**: ten blocks reading the same object reference one accessor rather than carrying ten copies. Deduplication is the *compiler's* job — it holds the whole graph and knows which pins are identical, so the device just stores what it is told and pays nothing at load or at runtime.

An accessor is immutable *after load*, but it is no longer cacheless: `vm_accessor_cache_build()` writes the resolution cache once, at load, before anything reads it. Sharing stays safe because the cached resolution is a property of the accessor, not of who is reading it — every sharer would resolve to the identical address. What it does mean is that an accessor must live in writable storage, which rules out putting one in flash and is why the loader builds them all in the arena.

Blocks hold resolved `vm_accessor_t*`, not ids. Ids are the wire form, pointers the runtime form; resolving once at load removes the invalid-id failure mode from every subsequent access, matching outputs (`vm_obj_h`) and `VM_OBJ_PTR` children. The cost is an ordering rule — accessors must exist before whatever references them.

An accessor and its index array are one allocation: identical lifetimes, always walked together. A `VM_IDX_NAME` index additionally owns a NUL-terminated copy of its name in the arena, because the wire form is unterminated and the frame buffer does not outlive the load.

**A `VM_IDX_REF` target must already exist when the referencing index is written.** That makes reference cycles impossible to *construct*, which is a stronger guarantee than `VM_ACCESSOR_MAX_DEPTH` catching them at resolve time.

**No resolution cache.** Once item counts became a shift, an unchained resolve costs about what checking a cache stamp would. Caching only pays on multi-level PTR chains, and spending ~24 bytes per accessor across every pin to speed up the rare shape is the wrong trade. If chain-heavy programs ever show up in profiling, the invalidation hook is `vm_obj_link()`.

### Reading and writing

Conversion is automatic in both directions: the caller's declared C type drives it, so the same block body works whatever the wired source stores.

```c
float angle = 0.0f;
BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), block);   // any stored type -> float
```

Float→integer conversion **rounds and saturates** (`vm_f_to_i`). A plain cast of an out-of-range float to an integer is undefined behaviour in C and is reachable from ordinary user wiring, not just corruption. Range is *not* clamped to the destination's own limits — narrowing 300 into a `uint8_t` still wraps, so clamp in a type wide enough to see the real value **before** narrowing.

| API | Purpose |
| :--- | :--- |
| `vm_obj_get_payload(&p, acc)` | resolve to where a value lives |
| `vm_get_obj(&h, acc)` | resolve to the object itself; follows a trailing PTR, but a count-0 accessor yields the container |
| `VM_OBJ_GET_VAL(out, acc)` | one converted scalar into a local |
| `VM_OBJ_SET_VAL(in, acc)` | converted write via accessor (checks `mutable`) |
| `VM_OBJ_SET_VAL_AT(in, obj, idx)` | converted write to an owned object, no resolve |
| `VM_PAYLOAD_GET_VAL(out, payload)` | read from an already-resolved payload |
| `vm_payload_at(p, i)` | element `i` as its own payload — the array-iteration primitive |
| `vm_obj_copy_content(src, dst)` | bulk copy; same type and count, PTR rejected |
| `vm_obj_link(join, owner)` / `vm_obj_link_direct(cell, idx, child)` | point a PTR slot at an object |

Every successful write sets the target's `upd` flag and honours `mutable`.

### Writing a block — what you have to remember

Nothing, in the normal path. A block body reads and writes pins exactly as before:

```c
float a = 0;
BLOCK_CALL(VM_OBJ_GET_VAL(a, in0), block);
BLOCK_CALL(VM_OBJ_SET_VAL_AT(a * 2.0f, out0, 0), block);
```

The resolution cache lives inside the accessor and is built by the loader, so an uploaded program gets it with no cooperation from the block. The one rule is for code that builds accessors **directly in C** — bring-up fixtures, tests, benchmarks — which must call `vm_accessor_cache_build(acc)` once the root object exists and the indices are set. Forgetting is a speed loss, never a wrong answer.

Two consequences worth knowing:

* A `static const vm_accessor_t` initialiser cannot be cached — it lives in flash and there is no build call. Correct, just slower. Fine for tests; use the build API for anything on a scan path.
* Binding is a load-time act. Re-pointing an id *after* its accessors were cached would leave them addressing the old object — and `vm_store_alloc()` refuses a rebind outright (`ERR_VM_REG_DUP`), so within one load this cannot happen at all; across loads the program is rebuilt whole.

**`VM_VAL_OF` and `VM_TYPE_OF` are a pair.** `VM_TYPE_OF` names the `vm_obj_t_e` that the reader (`VM_VAL_CAST_TO`) will use to pick a `vm_val_t` member, so `VM_VAL_OF` must write *that* member — `uint8_t`, `char` and `bool` write `.u8` because they are typed `VM_OBJ_U8`/`VM_OBJ_B`. Letting them fall to the `.u32` default reads back correctly on a little-endian target and silently wrong anywhere else; the arms are explicit so the pairing does not depend on byte order. Adding a type to one macro means adding it to the other.

**`resolve_fast()`** short-circuits the three accessor shapes that cover nearly every wire — `obj(id)`, `obj(id, [k])`, `obj(id, [j][k])`, literal indices only — as a few bounds checks instead of the chain loop. It returns `false` rather than an error for both "shape I don't handle" and "this failed"; the caller then falls through to `resolve_d()`, which re-walks and builds the real `err_h` with chain position and object in it. Failure is the cold path, so it is cheaper to pay for the walk twice there than to carry error construction through the hot one. Both paths must agree on what they accept — in particular the `mutable` check belongs on the deepest object in each.

---

## Blocks (`vm_block.h`)

One block is a single arena blob: `[cfg][in_cnt × const vm_accessor_t*][out_cnt × vm_obj_h][custom_data]`.

**Inputs and outputs are deliberately different shapes.** An input can be wired to anything — any object, any element, possibly through a switch cell, possibly with a dynamic index — so it needs a full accessor. An output is bound once at load to an object the block owns and never moves, so the slot holds the handle directly: smaller, and free at runtime.

### EN / ENO

Modelled on IEC 61131-3, and kept outside the numbered pins so the supervisor can gate every block identically without knowing which pin index a given block type uses. Both are ordinary boolean objects.

Several enable sources on one block combine per `en_mode`: **any** for a merge ("either branch reached me"), **all** for independent conditions. Both are needed — two branches of one split rejoining is an OR, while "on rising edge *and* while `A > B`" is an AND, and running that as an OR would drive the actuator on every edge regardless of the condition. Anything richer than those two is still an Expression block feeding one result in.

**Reading another block's ENO.** There is no special mechanism, because ENO is an ordinary object: the *producer* holds `cfg.eno` as a `vm_obj_h`, and a *consumer* reads it through a normal accessor rooted at that object's id. The same input/output asymmetry as every other pin — a block owns its outputs as handles and reaches everything else through accessors — and the same ids-are-the-wire-form rule that governs accessors.

To gate block B on block A's ENO:

| | |
| :--- | :--- |
| `0x42` | create a `VM_OBJ_B` object, 1 item, mutable → it gets an object id |
| block A | `cfg.eno` points at that object |
| `0x44` | create a chainless accessor `object(eno_id)`, `idx_count = 0` |
| block B | that accessor is one entry in B's enable list |

Two consequences worth planning for:

* **Every ENO is addressable for free.** `vm_loader_add_obj()` binds an id unconditionally, so there is no wire path to an unbound object — an ENO created by `0x42` is in the object registry like any other, and `vm_store_alloc()` rejects a duplicate id so two blocks cannot claim the same slot. (Only hand-built C fixtures can produce an unbound object, by calling `vm_obj_create()` without binding.) A block whose ENO is never consumed can leave `cfg.eno` NULL and skip the write entirely — `vm_block_set_ENO()` no-ops on NULL.
* **A chainless accessor is the cheapest shape there is** — `count == 0` is exactly what `vm_accessor_cache_build()` pre-resolves, so an EN read costs about what `flat[i]` does (~34 cyc), not what a chain costs. Gating is not a thing to be economical about.

**EN is a list, not a single accessor.** The flow graph is a DAG: a block reached by two branches has two enable sources. `cfg.en_mode` says how they combine — `VM_BLK_EN_ANY` for branches rejoining, `VM_BLK_EN_ALL` for independent conditions that must all hold. `en_cnt == 0` is a root that always runs, and below two sources the mode is irrelevant. The list lives in the block's own trailing bytes alongside the pins (`[cfg][inputs][outputs][en list][custom]`), so a root pays nothing and the common single-enable case pays one pointer — see [[VM_EXEC.MD]] for where the DAG comes from.

**A block carries no section id.** Section membership is the section's own ordered list of block ids, which is what the supervisor walks; a copy on the block could only disagree with it. `block_idx` does stay, because `BLOCK_CALL` runs inside block bodies where there is no supervisor frame to ask — and an error path relying on ambient identity would misattribute exactly when something is already wrong.

An EN that fails to resolve reads as **disabled**, not as enabled — pinned by stage F, so a broken gate stops a block rather than freeing it to run. That is the opposite of *absence* on purpose: no sources means nothing is gating this block, while a source that exists but cannot be read means the gate is broken. A failing source is reported and counts as false, so one broken branch cannot mask a working one that would have enabled the block.

```c
IF_BLOCK_ENABLED(block) {
  ... normal work ...
  vm_block_set_ENO(block, true);
} else {
  servo_go_home(...);           // defined safe state, not "whatever was last"
  vm_block_set_ENO(block, false);
}
```

The supervisor calls **every** block in a section every time that section runs, and never interprets `EN` itself; the block decides what being disabled means for it. Both halves are deliberate. If the supervisor skipped disabled blocks, an actuator could never define a safe fallback — "hold whatever was last commanded" is usually the wrong failure mode — and a block whose *active* state is `EN` false, such as an off-delay timer or a falling-edge detector, could never run at all.

Scheduling granularity is therefore the **section, not the block**: an idle event section costs nothing, but every block inside a section that runs is called. An unwired `EN` reads as enabled; a *failed* `EN` resolve reads as disabled, because running the body when the gate cannot be evaluated is the more dangerous guess.

`vm_block_is_enabled()` reports the level and nothing else. Edge detection and first-run initialisation are block-type concerns and live in that block's own `custom_data` struct — a block that wants an edge is stateful by definition and already has somewhere to keep it. Note such edges are observed at *section-run* granularity, not in real time.

`BLOCK_CALL(call, block)` is the reporting boundary: block `execute()` bodies are `void`, so there is nobody above them to return an `err_h` to. It wraps a failure with this block's identity and pushes it — the role `SE_ORIGIN_CALL` plays for top-level entry points.

---

## Program Loading (`vm_loader.h`) and Wire Protocol (class `0x04`)

`vm_store` owns the pool and all allocation; `vm_loader` owns the load state and all wire-facing validation. The decoder does framing only.

```
0x40 reset    : -
0x41 open     : u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u16 sec_cnt, u32 total_size
0x42 add objs : u8 n, n x { u16 id, vm_obj_head_t head, char name[head.d.name_size] }
0x43 set data : u8 n, n x { u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len] }
0x44 add acc  : u8 n, n x { u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len] }
                idx_data = idx_count x { u8 kind, LITERAL u32 | REF u16 acc_id | NAME u8 len + bytes }
0x45 add block: one block — header, in ids, out ids, en ids, private state
0x46 add secs : u8 n, n x { u16 sec_id, u16 start, u16 end }
```

All multi-byte fields little-endian. Design points:

* **`payload_size` in bytes, not an element count.** This reversed once: the wire used to carry `item_count` so a client needed no type-width table. Sizing is the client's job now, so it has that table anyway, and in exchange the device does **no arithmetic on sizes at all** — which removes the overflow that multiplying a 16-bit count by a width and truncating back into 16 bits used to risk. Two rejections replace it: `0` (`ERR_VM_OBJ_EMPTY`) because an object with no storage still has an address, and that address is the next allocation's first byte; and a size that is not a whole number of elements (`ERR_VM_OBJ_BAD_SIZE`), because 5 bytes of `U32` would let `vm_obj_elem_ptr()` return an element whose last three bytes lie past the payload. A wrong-but-aligned size is *contained* rather than rejected — every access is bounds-checked against it.
* **`start_idx` is unconditional.** Making its presence depend on comparing `byte_len` against the element width would leave framing dependent on a lookup the parser must trust, and would make a single non-zero element unaddressable (one element is never "larger than one element").
* **Flags travel as an explicit `VM_LOAD_F_*` byte**, not the raw header bitfield — bitfield layout is compiler-defined and a Python client cannot reproduce it reliably. `tagged` is derived from `name_len`; `upd` always starts clear.
* **For a `VM_OBJ_PTR` object, `0x43` data is a list of u16 child ids**, resolved through the table and linked. Children must therefore be created by an earlier `0x42`.
* `open` caps the arena at the **declared** `total_size` rather than the pool, so a program that under-declares then overruns fails on the object that overruns instead of silently borrowing space.

`vm_loader_reset()` detaches the table *before* resetting the arena, so for the whole teardown→load window every id resolves to NULL and accessors and late callbacks fail closed rather than into the previous program's storage. An abandoned or failed upload lands in the same safe state for free.

The class is registered in `sys_interface_init()` — see [[SYS_INTERFACE.MD]].

---

## On-Target Self Test

`components/runit/vm_selftest.c` exercises the stack bottom-up so a failure points at one layer rather than at "the VM". Enable with `RUNIT_ENABLE_VM_SELFTEST` in `runit_board_cfg.h`; results are PASS/FAIL lines on serial with a final tally.

Stages **P–S** cover execution: dispatch and what it refuses at load, section ranges and overlap, a pass run synchronously over a hand-built program (a block standing itself down, keeping its data, the `upd` sweep, holding while inactive, spans, `on_error`), and events. It dispatches through the real palette — the placeholders in `blocks/` — and reads their `calls`/`acts` counters out of the blocks' own private state, so there is no test-only seam in the exec layer at all.

Stage **T** is the first stage about a *real* block, and the first about a compiled input: fifteen expression programs, hand-assembled as RPN in the same layout the `0x45` record's private-state tail carries, run through `vm_exec_pass()`. It covers arithmetic and literals, a comparison feeding `SEL`, one pin named twice resolving once, the bitwise palette (masked shift, popcount, a shift of 32 landing on 0 rather than on undefined behaviour, a rotate), a declared-but-unnamed pin never being read — which is what the lazy input cache buys and what an eager prefetch would break — and then every fault: a zero divisor, an unknown opcode, a stack underflow, a program ending on two values, and `custom_data` too short for its own header. The fault assertions are about the *lifetimes* as much as the detection — malformed code latched in `VM_BLK_RT_CFG_BAD` and still latched a pass later, a math fault latched in the block's own `rt` and re-armed by the next clean evaluation, and in both cases nothing published and the flow withdrawn by the supervisor rather than by the block.

| Stage | Covers |
| :--- | :--- |
| A | header helpers and the type tables — every width a power of two, the shift table agreeing with the size table for all types, `total_size` with and without a name, `payload_at` stepping and its bounds |
| B | value conversion — widening, U64 round-trip at a 4-byte-aligned payload, float rounding into integers, `vm_f_to_i` NaN/saturation guards, narrow source types (`bool`/`char`/`uint8_t`) landing in the union member their tag is read back through, and the deliberate *absence* of destination clamping |
| C | accessor resolution — literal/by-ref/by-name indices, live by-ref following its selector, whole-array iteration, read-vs-write mutability, and the failure modes (OOB, unknown id, unlinked slot, chain past a scalar, depth cap) |
| D | nested objects — three-level chains, mixed literal/name steps, jagged 2D, `get_obj` at each depth, aliasing one child under two parents, re-linking as the switch/demux primitive, failures at depth |
| E | mutation — direct writes, `copy_content` including its type/count/mutability/PTR rejections, `link` and `link_direct` |
| F | block API — pin arrays, `get_in`/`get_out`, ENO writes, custom-data placement past all three arrays, and the enable list in full: no sources reading as enabled, `any` enabled from either side, `all` requiring every source and dropping as soon as one does, a single source behaving identically under both modes, an unresolvable source reading as disabled, and a broken source failing to mask a working one under `any` |
| G | object construction guards — every `vm_obj_create()` rejection still in its scope (bad type, zero payload, retentive PTR, NULL args), the flag round-trip, and that `upd`/`tagged`/`dynamic` are overwritten however the caller set them; 15-char tag placement at the payload tail, zeroing over dirty arena bytes, arena exhaustion. Oversize payload and over-long name moved to stage L, being the packer's checks now |
| O | dynamic objects — create/register/flag, tag and payload behaving as on any other object, arena objects inert to retain/release, the count holding at two and freeing at zero, a release cascade taking dynamic children while an arena child in the same tree survives, the register filling to `VM_DYN_MAX` and rejecting the next with `ERR_VM_DYN_FULL`, reset ignoring reference counts, and a rejected shape costing neither a slot nor an allocation |
| H | name matching and accessor construction — prefix/length/empty/over-long queries against real tags, NULL and untagged slots in the scan, accessors built through the construction API, index-setter and table-binding guards, fail-closed behaviour after a table reset |
| I | access edge cases — `get_obj` on a whole PTR container, scalar writes into PTR refused, out-of-range and negative by-ref indices, element-level `copy_content`, the depth cap from *both* sides, the `_Generic` arms stage B does not reach |
| J | string objects — per-character read and write, `char` through the `_Generic` path, buffer iteration, bulk copy and its length/type rejections |
| K | the upload protocol as **complete injected frames** through `sys_interface_decode()` — class dispatch, the decoder's cursor walk, the loader, then read-back through the accessor layer including a by-name lookup and accessor sharing |
| L | malformed frames — truncation, `name_len`/`byte_len` past the frame end, duplicate and out-of-range ids, writes past an object, unknown child id, unknown index kind, oversize program, unknown packet byte |
| M | the resolution cache and the **registry guards** — one binding path now covers all three kinds, so the checks that used to be duplicated per table live here: an id past the registry, a rebind, `VM_ID_NONE`, and each registry having its own id space. Plus that the cache is **invisible**: cached and uncached accessors agree on every answer including failures, a cached write still honours `mutable`, the excluded shapes refuse to cache, a re-link is still followed by the uncached chain, and a rebuild whose object is gone clears the flag rather than keeping a stale address |
| N | block upload — a 0x45 block driven in over the wire, then read back: header round-trip, ids resolved to **pointers** not kept as ids, EN/ENO binding, private state, and end-to-end pin read/write. A two-source enable list uploaded as a real `en_cnt == 2` record, both sources resolved, and the OR proven from either side. Then the reference failures that are the whole risk with blocks — unknown input, output, EN and ENO ids, duplicate and out-of-range block id, `in_cnt` past its limit, a `NO_ID` output, a record truncated in the inputs and again in the enable list — each of which must leave the id **unbound**. Plus the cases where `NO_ID` is *legal*: an input pin left unwired still reports `PIN_UNLINKED` rather than resolving to garbage |

Stages A–J and M drive the API directly; K, L and N inject real frames rather than calling `vm_loader_*`, so they cover the parts a direct API test would miss entirely: class registration, packet dispatch, and the cursor bounds checks. Expected-failure cases inspect the returned `err_h` without pushing it, so a clean run produces no error telemetry.

Several assertions exist to pin a decision rather than to check an obvious behaviour, and are worth leaving in place: that a tag prefix does **not** match (stage H), that a by-ref index of 65536 is out of range rather than truncated to element 0 (stage I), that a chain exactly at `VM_ACCESSOR_MAX_DEPTH` still resolves (stage I, so the cap cannot silently tighten), and that a rejected write leaves the target untouched (stages C and I).

`vm_selftest_inject(frame, len)` is exposed for ad-hoc probing — hand it any raw frame (class byte included) and it returns the decoder's `err_h` without pushing it.

Pair it with `RUNIT_SKIP_DEVICE_INIT` to boot with no I2C devices installed, which is useful when the VM is what you want to test and a missing or faulty peripheral would otherwise stall boot.

Both the test and the benchmark **yield one tick between stages** (and between benchmark scenarios), never inside a measurement. They run synchronously on `main`, which is priority 1 and blocks nowhere — no assertion or measurement waits on anything, and console logging does not yield either — so without it `IDLE0` never runs from boot init until the last stage finishes, and the task watchdog it feeds trips at `CONFIG_ESP_TASK_WDT_TIMEOUT_S` naming `main` as the CPU 0 hog. The two together are well past that, so it is not a matter of one slow stage. The tick costs nothing measurable: it lands outside the timed region, each scenario already warms the icache with a discarded call, and the figure reported is the best of three trials.

---

## Access Benchmark

`components/runit/vm_bench.c` times the access layer against the jagged 2D shape — an 8×8 float grid (`VM_OBJ_PTR[8]` of `VM_OBJ_F[8]`), read cell by cell. Enable with `RUNIT_ENABLE_VM_BENCH`; keep `RUNIT_ENABLE_VM_SELFTEST` **off** while benchmarking, since both drive the shared store and the test leaves it detached.

| Scenario | Shape | What it adds over the row above |
| :--- | :--- | :--- |
| raw C pointer walk | — | the floor: the data walk with no VM |
| `flat[i]` | 1 literal index | one resolve: id bounds check, header read, offset shift |
| `grid[r][c]` | 2 literal indices | one `VM_OBJ_PTR` dereference level |
| `grid[rsel][csel]` | 2 by-ref indices | two nested resolves per access |
| `grid["rN"][c]` | name + literal | a tag scan of the parent's children |
| row resolved once, payload walk | `vm_get_obj` + `vm_payload_at` | **removes** 56 of the 64 resolves |
| `grid[r][c]` write | 2 literal indices | the `mutable` check and the `upd` flag |

Each scenario performs exactly 64 accesses per pass and returns their sum, checked against 2016 before timing — a scenario that silently stops resolving would otherwise look extremely fast. Timing is `esp_cpu_get_cycle_count()` over 500 passes, best of 3 trials, with the CPU frequency measured rather than assumed. Interrupts stay live (BLE, timers), which is what "best of 3" is for: a trial that catches an ISR is discarded rather than averaged in.

The rows are ordered so each one isolates a single added cost. The last two are the ones to design against: **the payload walk is the shape a fold block should use** (resolve the row once, step its elements), and by-name access is the expensive one — right for a parsed message, wrong for a compiled path where the position is already known.

One assertion is worth knowing about: stage K asserts that `msg["temp"]` **without** a trailing `[0]` yields 0 rather than the value, pinning the documented two-step rule so a future auto-dereference change cannot silently pass.

### What the hot path is allowed to cost

The first benchmark run put `flat[i]` at 111 cyc against a 5.2 cyc raw walk. Disassembly, not guesswork, said where it went — and none of it was the walk itself:

* **A call.** `VM_OBJ_GET_VAL` invoked an out-of-line `vm_obj_get_payload`, so every pin read paid `entry`/`retw` register-window rotation and returned its result by writing 12 bytes of `vm_payload_t` through a pointer that the caller read straight back.
* **Two dispatches.** `vm_payload_read()` switched on the stored type into a `vm_val_t`, spilled it to the stack, and `VM_VAL_CAST_TO()` switched on the same type again to load it back.
* **Two flash data loads.** `vm_obj_type_sizes[]` for the width and `vm_obj_type_shifts[]` for the count — `.rodata` lives in flash, so both went through the data cache on every element access.
* **A `mull`.** The count came from a shift, then the address came from a multiply, to arrive at a number the shift already had.

The fixes, in the order they matter:

1. **`vm_resolve_fast()` moved into the header** (`__always_inline`). The three literal shapes resolve as straight-line code with the type and pointer live in registers. No call, no window rotation, no struct round-trip.
2. **`VM_LOAD_CAST_TO()` fuses load and convert** into one dispatch, replacing read-then-cast. `vm_payload_read()` remains for callers that hold a value rather than an address.
3. **`vm_obj_elem_ptr()` works in byte offsets.** `off = i << shift` is both the address and — compared against `payload_size` — the bounds check. No count, no width lookup, no multiply.
4. **`VM_OBJ_SHIFT_PACK`** replaces the shift table with a literal. Two bits per type in one immediate, and every valid width is `1 << shift`, so the size table leaves the hot path too.

A float read of `flat[i]` now ends in a single `lsi f0, a11, 0`, and the binary got *smaller* despite the inlining, because a fused switch is less code than two.

### Expression blocks

Three programs of rising size on the same machine, timed per **block execution** rather than per access — the whole of what a pass pays: trigger scan, `custom_data` check, bytecode walk, publish, ENO write. All eight pins are cached single-literal accessors on a dedicated input object, and none of the three uses a transcendental, so what is measured is the interpreter rather than libm.

| Program | cyc/exec | µs @ 160 MHz |
| :--- | ---: | ---: |
| 2 pins, 1 op | 473 | 2.98 |
| 4 pins, 3 ops | 793 | 4.99 |
| 8 pins, 20 ops + 6 literals | 2832 | 17.81 |

The three points fit **~233 cycles fixed + ~78 cycles per bytecode instruction**, and the per-instruction figure is flat across the two intervals (80 and 76) — a pin read, a literal push and an arithmetic op all cost about the same. Since a cached pin read is 27 cyc on its own and a float add is about two, that flatness says the per-instruction overhead, not the work, is what is being measured.

### Where the 78 cycles is not

Two plausible culprits were checked against the disassembly and **both are innocent**, which is worth recording so they are not chased again:

* **The opcode switch is not a linear compare chain.** GCC emits a *binary decision tree* — `bnei 5`, `bgeui 6`, `bnei 2`, `bgeui 3` … — so dispatch is about six branches. It is not a jump table (this target emits only 15 `jx` in the whole firmware, so the Xtensa cost model appears to avoid them), but a six-branch tree is 10–20 cycles, not 78.
* **The inlined fault paths are not the cost.** Every guard building its own error puts 70 `SE_alloc_bytes` + `vm_block_report_error` pairs in the function, at 2569 instructions. Hoisting them into one epilogue was tried: it collapsed those to 5 call sites and cut **3.2 kB** from the image, and it made every program **4–6% slower** — 473 → 502 cyc on the two-pin add, 2832 → 2945 on the twenty-op case. It was reverted.

### Where it is

Register pressure. The loop body reloads its variables from the frame every instruction — `l8ui a2, a1, 112`, `l8ui a10, a1, 134` — because Xtensa's windowed ABI leaves this function about twelve usable registers while the loop wants `pc`, `sp`, `loaded`, the code pointer and both struct pointers live at once, on top of a 128-byte frame for `st[16]` and `in[16]`.

That also explains the failed experiment exactly: an epilogue must be told where it went wrong, so `at`, `op`, the reason and the `err_h` all become live across the whole loop — four more spilled values, paid once per *instruction* instead of once per *fault*. The lesson generalises: in this loop, anything that adds a live value loses, and code size is not the currency.

The untried lever is the classic one for a stack interpreter — keep the top of stack in a local instead of in `st[sp-1]`, so a binary op costs one load rather than two loads and a store — with dropping the `in[]` pin cache (which buys a repeat mention 27 cyc but costs a live register on every instruction) as the cheaper half-measure.

That took `flat[i]` 111 → 66 cyc and `grid[r][c]` 141 → 105. It also made every *other* row slightly worse — by-ref 500 → 512, by-name 665 → 686, write 152 → 157 — because a shape the inline resolver does not handle now pays for the attempt before walking. That is the intended trade, but it exposed where the remaining cost actually was:

5. **A by-ref index no longer recurses when it does not have to.** `resolve_d()` called itself for every `VM_IDX_REF` step — a function with a 160-byte frame, re-entered twice per `grid[rsel][csel]` access, to read one `uint8_t` selector. It now tries `vm_resolve_fast()` on the sub-accessor first; only a selector that is itself chained or by-ref falls through to the recursion.

   **The depth cap has to be tested before that, not after.** `VM_ACCESSOR_MAX_DEPTH` is enforced by counting `resolve_d()` frames, and the fast path resolves a level without one — so letting it take the *last* level of a chain made `MAX_DEPTH + 1` fit inside `MAX_DEPTH` frames and pass. Stage C caught it: "one level past MAX_DEPTH is refused" failed while "exactly at MAX_DEPTH still resolves" passed, which is the signature of swallowing precisely one level. The fix declines the fast path at the limit and hands that level to `resolve_d()`, which refuses it and produces the same error trace as before. Worth remembering if any other shortcut is ever added below a bounded walk: a cap counted in frames breaks the moment something resolves without one.
6. **`VM_IDX_NAME` carries its length.** `find_child_by_name()` ran `strlen()` on the same constant string on every access. `vm_index_t` had padding after `kind`, so `name_len` costs nothing in size and is measured once at build. Use `VM_IDX_BY_NAME("temp")` for static initialisers — a hand-written `{.kind = VM_IDX_NAME, .name = "temp"}` leaves the length zero and matches **nothing**.
7. **Writes are inline too.** `vm_store_inline()` moved to the header, so `VM_OBJ_SET_VAL` and `VM_OBJ_SET_VAL_AT` resolve and store without a call — the latter matters most, since it is how every block publishes a result *and* what `vm_block_set_ENO()` runs for every block on every cycle. Only the error arms (`vm_obj_not_scalar_err`, `vm_obj_not_mutable_err`, `vm_obj_oob_err`, `vm_obj_null_obj_err`) stayed in the `.c`, where there is an ambient `OWNER` to build them with.
8. **`vm_get_obj()` uses the fast path** — it went straight to the walk, which cost the fold-block shape a full resolve per row.

9. **Read conversion split back out of line.** Fusing load-and-convert was right for speed and wrong for size: six arms — including a float→int64 saturation path and a U64→float conversion that drags in `__floatundisf` — measured ~450 B at *every* pin. One 2-input block came to 1.8 kB, and a 50-type palette to ~90 kB. Dispatching on the **destination** instead means only three shapes exist (float, ≤32-bit integer, 64-bit integer), so `vm_read_as_f32` / `_i32` / `_i64` — 370 B total, one copy — serve every pin in the program. `vm_resolve_fast()` stays inline, which is where the speed came from.

| symbol | before | after |
| :--- | ---: | ---: |
| one read site (`bench_flat_literal`) | 424 B | **252 B** |
| `vm_block_is_enabled` | 518 B | **300 B** |
| a 2-in/1-out block type | 1803 B | **1395 B** |
| one write site (`bench_grid_write`) | 543 B | 543 B |

Writes were deliberately left inlined. They are fewer than reads, they were the slower path, and the ~400 B they cost per site buys back the call — the right trade while flash is the plentiful resource and RAM is not.

> These figures were measured **before** EN became a list. `vm_block_is_enabled()` is now a loop over `en_cnt` sources rather than a single read, so its 300 B is stale in a direction not yet measured; the other rows are unaffected.

**Array iteration inverts the trade, and one round of measurement proved it.** `VM_PAYLOAD_GET_VAL` was routed through the shared converters along with everything else, and the payload walk went 21.7 → 39.1 cyc/element — 80% slower. A pin read runs its conversion once per block per cycle, so trading ~10 cyc for ~200 B across hundreds of sites is clearly right. A fold block runs it *per element*, with the type loop-invariant, across a handful of sites. So `VM_OBJ_GET_VAL` calls (`VM_LOAD_CAST_TO`) and `VM_PAYLOAD_GET_VAL` inlines (`VM_LOAD_CAST_TO_FAST`) — reach for the second only inside a loop over elements.

10. **Resolution cache, tier 1 only.** An accessor whose *address cannot move* is pre-resolved once by `vm_accessor_cache_build()` and thereafter costs four loads instead of a walk. Two shapes qualify: a whole-object accessor, and a single literal index on a root object.

    **Why those two and nothing deeper.** Neither traverses a `VM_OBJ_PTR`, so nothing a running program does can invalidate them — no generation counter, no invalidation, and no pathological case. A two-level chain is deliberately excluded: its address depends on a link `vm_obj_link()` can move underneath it.

    A single literal index on a *pointer* object is cached, and safely so: what is cached is the address of the pointer **slot**, not the object behind it. `vm_get_obj()` still dereferences the link live, so re-linking is picked up exactly as before. That is the whole reason this tier needs no invalidation machinery.

    The loader calls it at the end of `vm_loader_add_accessor()`, where objects already exist. Code that builds accessors directly must call it; **skipping it costs speed and nothing else** — the cache is never a correctness input, and an accessor that was never built simply resolves the long way. `vm_loader_reset()` reclaims the arena, so a reload starts every accessor uncached.

    Cost: `vm_accessor_t` grows 8 → 20 bytes. For a 100-block/500-pin program that is **+6 kB**, against a ~19 kB program total.

    Measured, at 160 MHz:

| scenario | before | after | |
| :--- | ---: | ---: | ---: |
| `flat[i]` | 76.5 | **34.4** | −55% |
| payload walk | 21.7 | **16.8** | −23% |
| `block_add` scalar | 257.9 | **156.6** | −39% |
| `grid[r][c]` | 105.6 | 113.6 | +8% |
| `grid[r][c]` write | 138.7 | 147.6 | +6% |
| by-name | 659.5 | 672.3 | +2% |

    **The uncached shapes pay ~8 cyc for a flag test they always fail.** That is the same trade the inline fast path made and it is worth it here — a `block_add` on cached pins dropped 100 cycles — but it means the two-level chain `grid[r][c]` is now the most expensive *common* shape, since it is both uncacheable and taxed. If array-of-structs wiring turns out to dominate real programs, the fix is not a generation counter but static proof: the compiler knows whether any switch block writes a given cell, so a chain through a cell nothing re-links could be marked cacheable at upload time and keep the invalidation-free property.

    Stage L pins the property the tier rests on — that the cache is *invisible*. A cached accessor and an identical uncached one must agree on every answer including failures; a cached write must still honour `mutable`; the excluded shapes must refuse to cache; and a rebuild whose object has gone must clear the flag rather than leave a stale address behind it.

11. **The tag scan called `memcmp()` — as a library call.** `find_child_by_name()` filtered on length and first character, then handed the rest to `memcmp(tag + 1, name + 1, n - 1)`. The length is not a compile-time constant, so GCC emitted a real call: `call8` + `entry` + generic prologue + `retw`, to compare **one byte** for a two-character tag. Every child that shared a prefix paid it, which parsed message fields routinely do (`temp`/`time`, `val1`/`val2`).

    The arithmetic pinned it before the fix: by-ref and by-name run the identical two-level shape through `resolve_d` and differed by 672 − 405 = 267 cyc, almost exactly 4.5 candidates × one call-plus-`memcmp`. An open-coded byte loop replaces it — a tag is at most `VM_OBJ_NAME_MAX`, so there is nothing a library call can do better.

12. **`resolve_d`'s cold arms moved out of its body.** Eight `SE_ERR_NEW` sites had pulled `strlen` and a pile of payload construction into the walk. They are now `noinline` helpers; `SE_ERR_NEW` records no file or line, so the trace is unchanged — the tag, the ambient `OWNER` and the payload fields carry all of it. `resolve_d` went 1306 → 1198 bytes.

    **The stack frame did not move: still 144 bytes.** That was the stated reason for doing this and the prediction was wrong — the frame is driven by the recursion's own locals and inlined struct returns, not by error payloads. Worst-case depth cost is ~1.15 kB, essentially where it started, so **the untrusted-input stack item remains open**. Closing it properly means converting the by-ref recursion into an iterative walk over a fixed-size stack, bounded by construction rather than by a depth constant.

    A second attempt at the frame — taking all of `payload_as_index()` out of line — was measured and reverted: it cost a by-ref access two calls (**405 → 418 cyc**), left the frame at 144 anyway, and its only real benefit was keeping `roundf`/`__fixsfdi` out of the walk. Only the float arm (`index_from_float`) is out of line now; the integer arms are a load and a cast and belong inline. Worth remembering as the pattern: evict the arm that drags a library in, not the function that contains it.

**Considered and rejected:** a `flags` byte on `vm_accessor_t` classifying the shape at load time, so `vm_resolve_fast()` could skip its kind checks and slow shapes could skip the attempt. It is worth ~5 cyc, and an opt-in bit means any accessor built by hand rather than through `vm_accessor_create()` silently loses the fast path. Not worth a footgun that reports nothing.

Point 4 is the one to re-measure rather than trust. A 9-byte table stays in the data cache inside a tight benchmark loop, so the packed constant may not beat it there; it is chosen for the real scan cycle, where the data cache is busy with actual object payloads and the literal rides the instruction path instead. If the benchmark says otherwise, a 16-entry table indexed by the raw 4-bit type — with invalid entries marked `0xFF` so one load covers both the shift and the validity check — is the alternative worth trying.

---

## Packet → Objects: the whole load path

Five packets, four components, one arena. Nothing is allocated outside it and nothing survives a reload.

```
BLE write
   │
   ▼
sys_interface_handle_frame(data, len)          components/system/sys_interface
   │  data[0] = class header, matched against registered classes
   ▼
dec_vm_loader_decode(data+1, len-1)            components/codecs/decoders
   │  data[0] = packet header 0x40..0x44, walks records with a bounds-checked cursor
   ▼
vm_loader_*()                                  components/VM/core/loader/vm_loader.c
   │  state machine + wire→internal translation
   ▼
vm_obj_create / vm_obj_link_direct /           components/VM/core/obj/vm_obj_build.c
vm_accessor_create / vm_accessor_cache_build
   │
   ▼
vm_store_alloc()  →  pool malloc'd at 0x41 open components/VM/core/store/vm_store.c
```

The class is bound once in `sys_interface_init()`:
`sys_interface_register_class(VM_LOADER_CLASS_HEADER, dec_vm_loader_decode, "vm_loader")`.

### The five packets

| | packet | payload | effect |
| :--- | :--- | :--- | :--- |
| **0x40** | reset | — | `vm_loader_reset()` |
| **0x41** | open | `u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u16 sec_cnt, u32 total_size` | reset, cap arena, build all four id tables |
| **0x42** | add objects | `u8 n`, then `{u16 id, vm_obj_head_t head, char name[]}` | create + bind each object |
| **0x43** | set data | `u8 n`, then `{u16 id, u16 start_idx, u16 byte_len, u8 data[]}` | fill a payload, **or link children** |
| **0x44** | add accessors | `u8 n`, then `{u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[]}` | create + pre-resolve each accessor |
| **0x45** | add block | one block: header, in ids, out ids, en ids, private state | check the type is registered, create + resolve all wiring |
| **0x46** | add sections | `u8 n`, then `{u16 sec_id, u16 start, u16 end}` | bind `[start, end)` ranges over the block order |

0x42, 0x43 and 0x44 are batched and repeatable — a program arrives across as many frames as the MTU needs. **0x45 carries exactly one block**, because a block record varies in length four separate ways (inputs, outputs, enable sources, private state); batching would need a length prefix per record and a cursor that can desynchronise, and one-per-frame puts a malformed record's blast radius at that block.

### One store, four registries

Objects, accessors, blocks and sections are four id spaces over **one** arena, and they go through one API — [vm_store.h](core/store/vm_store.h):

```c
vm_store_open(total, counts)     // size the arena, build the registries
vm_store_alloc(&p, r, id, n)     // carve, zero, bind -- in one step
vm_store_get(r, id)              // id -> pointer, NULL past the end
vm_store_reset()                 // detach every registry, then reset the arena
```

They had been three copies of the same code: three table structs, three `build`/`set`/`reset` triples, three pairs of out-of-range/duplicate error tags, differing only in the pointer type. Now `vm_obj_by_id()`, `vm_accessor_by_id()` and `vm_block_by_id()` are one-line typed casts over `vm_store_get()`, and the three construction functions are the only things that know what a chunk means. Six error tags collapsed into `ERR_VM_REG_OOB` / `ERR_VM_REG_DUP` carrying a `kind`, so a trace still reads *"accessor registry: id 7 is already bound"*.

`vm_store_alloc()` also folds three steps that were previously separate and individually forgettable — allocate, zero, bind — into one. Zeroing is not optional: the arena hands back whatever the last program left there, and every consumer reads as "empty" only if those bytes are clear. The id is validated **before** any arena is spent, so a program with a bad id leaves the space its retry needs.

`VM_ID_NONE` allocates without binding, for storage reached through something else — an accessor's copied name bytes — or built by a block at runtime.

**All three allocate the same shape: header, then trailing bytes, in a single chunk.** An object is `[head][payload][tag]`, a block is `[cfg][ins][outs][custom]`, an accessor is `[header][indices]`.

The accessor is the one that expresses it differently: `indices` is a *pointer* into its own tail rather than a flexible array member. A FAM was tried and reverted — it saves 4 bytes and one dependent load per chain step, but C forbids a struct containing a flexible array member from being a member of another struct (C11 6.7.2.1p3), which is exactly what a compile-time `static const` accessor needs. The alternatives were a GCC extension or a hand-duplicated layout no assertion could protect. Since the resolution cache already removes that load from every shape that runs hot, the trade came down to 4 bytes against losing compile-time accessors entirely.

### Step by step

**0x40 — reset.** `vm_loader_reset()` calls `vm_store_reset()`, which detaches all three registries *before* resetting the arena. For the instant in between, every id resolves to NULL, so an accessor or a late callback finds nothing rather than storage about to be handed out again.

**0x41 — open.** Resets first, so an upload that restarts mid-stream cannot mix two programs. `total_size` is checked against the pool (`ERR_VM_LOAD_TOO_BIG`, carrying requested-vs-available), then the arena is capped at the **declared** size rather than the whole pool — a program that under-declares fails on the object that overruns, which points at the bug, instead of quietly borrowing space it never asked for. Both id tables are bump-allocated and zeroed. State becomes `VM_LOAD_OPEN`.

**0x42 — objects.** The loader passes the received `vm_obj_head_t` directly to `vm_obj_create()`, which takes that head plus a name pointer — there is no separate config type. The name is copied straight out of the frame, because `name_size` in the head says how many bytes to take, so the wire form needs no NUL and no staging buffer.

`payload_size` arrives already in bytes, so this layer does no arithmetic on sizes — nothing to overflow, nothing to truncate. The type is checked before use (`ERR_VM_OBJ_BAD_TYPE`). `vm_obj_create()` then checks what it still can — type, zero payload, the retentive-PTR rule — and hands the size to `vm_store_alloc()`, which carves 4-aligned, **zeroes** it (the arena still holds the previous program's bytes) and binds the id, rejecting duplicates and out-of-range ids before spending any arena. The head is copied in, `upd`/`tagged`/`dynamic` are forced to the creator's values whatever the caller asked for, and the tag is appended.

Passing a wrong `payload_size` is therefore possible, and contained: `vm_obj_elem_ptr()` bounds every access against it, so an undersized object refuses reads past its end rather than reaching into its neighbour.

**0x43 — data, and the tree.** Two paths, chosen by the object's own type:

* **`VM_OBJ_PTR`** — the bytes are a list of `u16` **child ids**, never addresses: a pointer is meaningless outside this boot's arena. Each child must already exist, so a forward reference is a hard error rather than a dangling slot. This is where the object *tree* forms.
* **anything else** — `byte_len` must be a whole number of elements and `start_idx + n` must fit, then a straight `memcpy` into the payload.

**0x45 — blocks, and why they are last.** A block names everything else: inputs and EN are accessor ids, outputs and ENO are object ids. Every one must already be bound, which is the same ordering rule a `VM_OBJ_PTR` parent has with its children, one level up. All of them are **resolved to pointers here, once** — the block stores no ids at all, so nothing downstream can hold one that stopped being valid, and no execution path ever pays a table lookup.

The id is checked against the block table *before* anything is built, and every reference is resolved *before* anything is allocated: a rejected block costs the arena nothing, so the retry still has the space it needs. `VM_BLOCK_NO_ID` (0xFFFF) now spells "absent" for **ENO only** — the block publishes nothing. An absent EN has no such spelling because it does not need one: it is simply `en_cnt == 0`, and a *listed* enable that fails to resolve is a malformed program, exactly like an unwired numbered pin.

Three shape limits, all enforced at load — `in_cnt`, `q_cnt` and `en_cnt` each ≤ 16 — for one reason: the decoder stages every id list on its stack while resolving, and a `uint8_t` count would make that a 510-byte buffer inside a BLE callback. Sixteen is well past any real block shape.

**0x44 — accessors.** Index records are decoded by kind: `LITERAL` takes a `u32`; `NAME` copies its bytes into the arena NUL-terminated and stores `name_len` so the scan never calls `strlen`; `REF` names an accessor id that **must already exist** — which is what makes a reference cycle unconstructable, rather than merely depth-capped later at resolve time. The id is bound by `vm_store_alloc()` as part of the same chunk that holds the index array, and `vm_accessor_cache_build()` then pre-resolves the accessor if its shape qualifies.

### Ordering rules

| requirement | if violated |
| :--- | :--- |
| `0x41` before everything | `ERR_VM_LOAD_BAD_STATE` |
| a child object before the `0x43` that links it | `ERR_VM_ACCESSOR_UNKNOWN_ID` |
| a referenced accessor before the one referencing it | `ERR_VM_ACC_TABLE_OOB` |
| every object and accessor a block names, before the block | `ERR_VM_BLK_BAD_REF`, naming the slot |
| a block's type registered in the palette, before the block | `ERR_VM_BLK_UNKNOWN_TYPE` |
| every block a section covers, before the section | `ERR_VM_SEC_BAD_RANGE` |
| a root object before the accessor rooted on it | **no error** — the accessor stays uncached, costing speed only |

### On failure

Every stage returns an `err_h` chain up through the decoder to `sys_interface`. The loader stays `OPEN` with the arena partially consumed — nothing is rolled back, because nothing needs to be: an id that was never set still reads NULL, so a half-built program is never *incorrectly* resolvable, only incomplete. The next `0x40` or `0x41` clears it.

### Where this path currently ends

At **a program that runs**, with an empty palette.

A program uploads its objects, its accessors, its blocks and its sections; every block comes out with resolved pins, EN, ENO and zeroed private state, and its `block_type` checked against the palette. `vm_exec_pass()` then walks the section ranges in order, decides activation for each block, dispatches through the descriptor, drives non-activating blocks' outputs to their defaults quietly, honours `cfg.on_error`, and clears `upd` at the end of the pass. `vm_exec_start()` puts that loop on core 1.

What is still missing is the **real palette** — blocks that do something. `blocks/` currently holds seven placeholders with the right shapes and no behaviour, which is what the self test dispatches through (stages P–S) and what a real block gets copied from. A `0x45` naming any other type is rejected with `ERR_VM_BLK_UNKNOWN_TYPE`, by design rather than by omission: a type nothing dispatches is better refused at load than discovered on a pass.

---

## Execution (`core/exec/`)

The model this implements is [[VM_EXEC.MD]] — read that for *why*. This is what
exists in code.

### The pass

```
for each bound section:  freeze point -> run [start, end) atomically
end of pass:             sample subscriptions -> clear upd
```

`vm_exec_pass()` runs one pass on the calling task; `vm_exec_start()` puts that
loop on a task pinned to **core 1**, yielding one tick per pass. At
`CONFIG_FREERTOS_HZ = 100` that one tick *is* the 10 ms floor, and it is also
what feeds core 1's idle-task watchdog — `taskYIELD()` would not, since idle
runs at priority 0.

A **freeze point** is the only place anything external intervenes: queued
events become visible, overflow errors are raised, and freeze/resume take
effect. Nothing lands mid-section, so a section always traverses one snapshot.

If no section is bound, the whole block order runs as one section. That covers
both "the program declared none" and "its section packets never arrived" — the
alternative for the second case is a program that loads, counts passes and
executes nothing, with nothing to see.

### Activation is the block's, not the supervisor's

**Every block in a running section is called.** The supervisor tests nothing,
gates nothing and skips nothing.

That is where the knowledge is. A block knows what wakes it — an arrival, a
level, a deadline, nothing at all — better than any table describing it from
outside, and it already holds everything needed to answer. So it asks:

| helper | question |
| :--- | :--- |
| `vm_block_triggered()` | did anything new arrive on any of my inputs? |
| `vm_block_input_fresh(pin)` | was it *that* one? |
| `vm_block_is_enabled()` | do my enable sources say so? |
| `vm_block_set_ENO(b, false)` | not acting — and this is the *entire* else-branch |

and the ordinary block body is:

```c
static void blk_expression(vm_block_h b) {
  IF_BLOCK_TRIGGERED(b) {
    ...
    vm_block_set_ENO(b, true);
  } else {
    vm_block_set_ENO(b, false);         // nothing arrived, so nothing changed
  }
}
```

**A block that does not act keeps its outputs and drops its ENO. Nothing else.**
Nothing changed, so last pass's answer is still the right one — an expression
whose inputs are all old computes the same result, and zeroing it would publish
a 0 where a correct number was standing. Data persists; what is withdrawn is
the flow.

Nothing else is needed because **the supervisor already withdraws freshness for
everybody**: the end-of-pass `upd` sweep means an output nobody rewrote reads
as stale on the next pass for free. So both kinds of downstream block stand
down on their own — one gated on the ENO level, one waiting on an arrival that
never came — and the flow below prunes itself by data.

There is no "clear my outputs" call, and there was one. It existed for outputs
carrying *flow* rather than data — a router's `YES`/`NO` — on the theory that a
router which fired once would otherwise leave `YES` true forever. That only
bites if something reads `YES` as a **level**, and an event router's consumers
are update-driven: they gate on freshness, which the sweep already withdraws.
The call was solving a problem the sweep had already solved.

The write is **asymmetric, and `vm_block_set_ENO()` owns the asymmetry** so no
block body can get it wrong: **true is loud** (an assertion of flow is news, so
it sets `upd`), **false is quiet** (a loud false would read as an *arrival* to
an update-driven block below — the exact opposite of standing down).

An actuator's `when inactive: Hold` then needs no mechanism at all — the block
is called like every other and simply never sets ENO false, holding its output
and its flow alike.

`core/block/vm_block_template.h` carries a worked template per shape.

`vm_block_triggered()` latches `VM_BLK_RT_TRIGGERED` in `cfg.rt`, so the answer
survives the call for telemetry and debugging. Blocks that never ask pay
nothing for the feature existing.

### The palette

One `const` array in `blocks/vm_blocks_table.c`, indexed by `block_type`. That
is all of it — there is no palette module, no registry, no registration call
and no indirection:

```c
const vm_block_fn g_vm_blocks[] = {
    /* [VM_BLK_NONE] stays NULL: an unset block_type must not be runnable. */
    [VM_BLK_NOP]   = vm_blk_nop,
    [VM_BLK_GATE]  = vm_blk_gate,
    [VM_BLK_ROUTE] = vm_blk_route,
};
```

Everything a block needs is already in the handle — pins, enable list, outputs,
private state — so there is nothing for a descriptor to hold that the block
cannot read directly, and nothing for the supervisor to decide on its behalf. A
designated initialiser *is* the registration, the table lives in flash, and
adding a block type is its own `.c` plus one line here.

That did cost something: a central table is a two-place edit, which is exactly
what a registration API avoided. It was worth it. A table you can read at a
glance beats a mapping you have to grep for, and a mutable array, a duplicate
check, a slot-range check, an init order and two error tags all went with it.
One gap remains: C lets a later designated initialiser silently override an
earlier one, so the component builds with **`-Woverride-init`** to make a
doubly-claimed type a warning rather than a last-one-wins surprise.

A firmware has exactly one palette, so the array *is* what dispatch indexes —
nothing can point it somewhere else. It lives beside the blocks rather than in
`core/exec/` on purpose: the supervisor must not know what is in the palette,
it dispatches through the declaration in `vm_exec.h` and nothing more, so the
dependency runs `blocks/` → `exec/` and never back. That is what keeps `core/`
free of every driver a real block will eventually pull in.

**Type 0 is reserved.** An unset `block_type` is zero, and it must not resolve
to something runnable.

`vm_exec_check_block_type()` runs at load and refuses exactly one thing: a type
no function claims. Otherwise the block would load fine and be silently skipped
on every pass — a program that runs, reports nothing, and does less than it says.

What is deliberately **missing** is a description of what a block *takes in* —
pin counts, private-state size, types. That is load-time validation and editor
material, and it comes back as its own table when there is a palette to
describe. Until then a wrong `custom_len` is the program's own problem.

### What ships today

Seven **placeholders** (`blocks/vm_blocks.h`): `NOP`, `GATE`, `ON_UPDATE`,
`HOLD`, `REPEAT`, `FAULT`, `ON_EVENT`. They have the shapes real blocks have and
none of the behaviour — each decides whether to act exactly as its kind of
block would, then publishes a `1` instead of doing anything, and keeps
`calls`/`acts` counters in its private state so the gap between "was
dispatched" and "decided to act" is observable.

They earn their place three ways: they are the templates in
`core/block/vm_block_template.h` made real, one per shape; they give the self
test something to dispatch, so standing down, holding, span claiming and
`on_error` are actually executed rather than merely compiled; and a real block
starts by copying the one nearest its shape.

Two **real** blocks (`blocks/vm_block_expr.h`), numbered from where the
placeholders stop because the id space is one space: `EXPR` (8) and `EXPR_BIT`
(9).

### Expression blocks

One machine with two words — `EXPR` evaluates in `float`, `EXPR_BIT` in
`uint32_t` — over an RPN instruction stream the client compiles and ships in
the block's own `custom_data`:

```
u8 const_cnt | u8 rt | u16 code_len | u32 consts[const_cnt] | u8 code[code_len]
```

The literals precede the code so they land 4-aligned with no pad: `custom_data`
always starts 4-aligned and the header is exactly 4 bytes, so reading one is an
aligned load. `rt` is the block's own scratch byte and must be zero on the wire.

They are two block types rather than one with a mode byte because the palettes
barely overlap — there is no sensible `ROL` on a float, no `SQRT` on a bit
pattern — and a mode byte would make every program carry the answer to a
question `block_type` already asks. Only the first six opcodes (`END`, `IN`,
`K`, `DUP`, `DROP`, `SWAP`) are numbered alike, so an encoder or disassembler
can handle pushes and stack plumbing without knowing which block it is aimed at.

**Straight-line, by construction.** There is no jump, so an expression cannot
loop and its worst-case duration is `code_len` — which is what lets one sit
inside a section whose duration has to stay computable. A branch is `SEL`, which
evaluates both arms; a loop is `REPEAT` and a span.

**Activation is `IF_BLOCK_TRIGGERED` alone; the enable list is not consulted.**
An expression is a function of its inputs, so re-evaluating one nothing arrived
on has a known answer. A program wanting one gated wires the gate downstream,
where a level belongs. Pins are read at most once each per evaluation and only
if the code names them, so every occurrence of `IN 2` is the same number even
through a dynamic accessor, and a declared-but-unused pin costs nothing.

**Two fault classes, two lifetimes, because this runs at 100 Hz.** A malformed
program (`ERR_VM_EXPR_BAD_CODE` — bad opcode, truncated operand, stack under- or
overflow, a stack not ending on exactly one value) is a standing condition, so
it is reported once and latched in `VM_BLK_RT_CFG_BAD`, exactly as a malformed
span is. A bad *value* (`ERR_VM_EXPR_MATH` — zero divisor, negative square root,
a result that came out non-finite) belongs to this pass's data: it is reported
on entering a fault episode and re-armed by the next clean evaluation, so a
divisor stuck at zero for a minute is one error rather than six thousand. Either
way nothing is published — the output stands and `cfg.on_error` decides the flow.

`VM_BLK_RT_CFG_BAD` (0x40) is generic, not expression-specific: any block type
can latch "my private state is malformed" in it and report once per load.

### Spans

A block that repeats or skips part of the order keeps the range in its own
private state, like any other parameter, and **claims** it at run time with
`vm_block_claim_span()`. That sets `VM_BLK_RT_SPAN`; the walker reads the bit
*after* the call and jumps, because only the block knows it is a span owner.

The contract that matters: **claim first, unconditionally**, before deciding
anything else. A FOR that returns early without claiming — disabled, or count
zero — leaves the walk about to run the span inline, which is the one outcome
neither branch wanted. Running a span zero times is not the same as letting
somebody else run it once.

Nesting is capped at `VM_EXEC_MAX_SPAN_DEPTH`. A claim that doesn't move the
walk forward is reported once (sticky `VM_BLK_RT_SPAN_BAD`) and then ignored,
so it degrades to running inline rather than to a hang.

### `upd`, and who clears it

The supervisor, at the **end of a pass** — not at the point of consumption
(which would break fan-out: two blocks reading one fresh object, and whichever
ran first steals the update) and not per section (which would make a value
written in section 0 read as stale in section 3 of the same pass). The sweep
respects `upd_resetable`, and covers dynamic objects as well as the registry.
Subscriptions are sampled *before* it, via `vm_exec_set_sample_hook()`.

### The block watchdog

One store per block, no clock reads: the supervisor publishes `(seq << 16) |
block_id` to a volatile, and a 20 ms `esp_timer` samples it. Same word twice
running means that block hung, and the error **names it** rather than just
resetting the chip. The sequence number is what distinguishes "still running"
from "running again next pass".

[[VM_EXEC.MD]] specifies a timer ISR here with a deferred report. This uses an
ordinary `esp_timer`, whose callback already runs at task level on core 0 — so
it keeps sampling while core 1 is stuck, and can build the error where it
detects it. The deferred hand-off would have been worse: a genuinely hung block
never reaches the freeze point that would have drained the flag.

### Events

**An event lives exactly one scan cycle.** A FreeRTOS queue and a snapshot:

```
sys_callbacks routes here  ->  [ queue ]  -- drained at the top of a pass -->
[ this cycle's events ]  <-  blocks query here, until the next drain
```

A pass therefore sees one unchanging snapshot of everything that arrived while
the previous pass was running, and whatever it does not act on is overwritten
at the next drain. Events **can** go unhandled, on purpose: a block that was disabled
when its event arrived simply misses it, which is the same answer a physical
edge gives.

The drain replaces a hand-rolled queue's entire apparatus — no per-entry
lifetime, no sequence numbers, no visibility watermark, no ordering maintained
across passes — and the cost of it going wrong is one lost event rather than a
corrupted stream.

**What is buffered is `cb_event_t`** — the system's own callback event, exactly
as [[SYS_CALLBACKS.MD]] routed it. There is no VM event type, on purpose: a
VM-side struct would mean a translation step that has to be extended every time
a new callback kind appears, and would throw away what the head already carries
(`callback_type`, `route_mask`, `action_id`). GPIO edges, power faults and BLE
traffic are the whole point of this buffer, and they already have a shape.

The VM plugs in as **route slot `SYS_CB_ROUTE_VM` (4)**, registered from a
load-time constructor like every other domain. Unlike the other slots it does
not *handle* the event — it parks it for one scan cycle and lets blocks decide.

**Blocks ask; nothing is delivered, and nothing is addressed to a block.** An
event block is not woken and not routed to. It walks `[0, vm_event_count())`
with `vm_event_at()`, recognises what it cares about from the head and the
union, and either acts or stands down. Nothing is consumed, so two blocks
watching the same GPIO both see the edge — there is no ownership to arbitrate
and no order to get wrong. A driver reports "GPIO 4 rose" knowing nothing about
the program; any number of blocks may act on it, or none.

**There is no locking of the VM's own.** Producers are `sys_cb_task` (core 0);
the consumer is the supervisor (core 1), and they genuinely run at once — but a
FreeRTOS queue already arbitrates that correctly, so there is nothing here to
get wrong. After the drain, this cycle's events live in an array no other task
can see, which makes every read a block does lock-free *by construction* rather
than by argument.

`vm_event_post()` is **not** called from an ISR, despite being the far end of
an interrupt. `sys_callback_trigger()` already absorbed that with
`xQueueSendFromISR`, and `sys_cb_task` dispatches route functions at task
level, so ISR safety was handled a layer down. It never waits, though: a
producer that blocked would stall `sys_cb_task` and with it every other route —
an event the VM has no room for must not cost `sys_io` its own delivery.

An earlier version hand-rolled a double buffer under a `portMUX`. It worked,
but it was two buffers, an index, a spinlock and a swap-time race to reason
about, reimplementing what `R_QUEUE_DEFINE` gives for free.

Overflow — more than `VM_EVENT_DEPTH` in one cycle — drops the newest and
raises `ERR_VM_EVENT_OVERFLOW`. Events dropped by the *drain* for want of a
taker are normal, and silent.

### `usr_mutable`, finally read

`mutable` is "may be written at all"; `usr_mutable` is "may be written by *user
logic*". A block's own ENO is the first and not the second, so a Set block the
user drew cannot forge another block's flow. Internal writes keep using
`vm_obj_set_scalar()`; user-authored and remote writes go through the `_usr`
forms, which check both.

---

## Program RAM Budget

The pool is **allocated per load**, at exactly the size the program declares in `0x41`, from internal DRAM. It holds **everything a program owns** — objects, accessors, blocks and all three registries. A 5 kB program costs 5 kB; the other ~43 kB a fixed reservation would have held stays with BLE and the heap. `VM_STORE_MAX_POOL` (128 kB) is only a sanity ceiling so a corrupt `total_size` is rejected cleanly rather than attempted.

| item | bytes |
| :--- | :--- |
| block | 16 (`vm_block_data_t`) + 4/input + 4/output + 4/enable source + own variables |
| object | 4 (head) + payload, 4-byte aligned, + tag bytes if named |
| accessor | 20 + 8 per chain index — shared between pins reading the same thing, so unshared is the worst case |
| registries | 4 per id, in each of the three id spaces |

Worked from the target workload — 100 blocks, 5 pins each, one 8-byte output apiece, 16 B of own variables, **no** accessor sharing:

| | | bytes |
| :--- | :--- | ---: |
| blocks | 100 × (20 + 20 + 4 + 16) | 6.0 k |
| outputs | 100 × (4 + 8) | 1.2 k |
| constants | 100 × (4 + 8) | 1.2 k |
| accessors | 500 × (20 + 8) | 14.0 k |
| registries | 200 obj + 500 acc + 100 blk ids | 3.2 k |
| | **total** | **25.6 k** |

That figure is a **reference for the client's own estimate**, not a device-side reservation — there is no fixed pool to size against. The client declares what its program needs in `0x41` and the device allocates exactly that, per load. Scaling the same shape to the ~300-block ceiling the target workload assumes lands near 77 kB.

A program that does not fit — past `VM_STORE_MAX_POOL`, or larger than the biggest contiguous block internal DRAM can currently give — fails its load with `ERR_VM_LOAD_TOO_BIG` carrying requested-vs-available. Fragmentation is the real limit rather than free-heap total, which is why the error reports the largest available block: a client can tell "no room at all" from "no room in one piece".

Note the app **partition** is 1 MB (`CONFIG_PARTITION_TABLE_SINGLE_APP`), not the chip's 8 MB. Code size is bounded by that partition, currently 649 kB used of 1 MB.

---

## Design Notes & Known Limitations

* **Everything below the self test is covered.** `vm_selftest` covers object, accessor, loader layers, blocks, EN/ENO, and performance benchmarks.
* **No block types palette yet.** The pin/EN/ENO/error scaffolding is in place; the standard block palette is expanding.
* **No block types are implemented.** The pin/EN/ENO/error scaffolding is in place; the palette is not.
* **Subscription and telemetry are not built.** The design is settled (supervisor iterates a list of ids, sends `[id][content]` on `upd`, client rebuilds objects from a manifest) but no code exists. Note `VM_OBJ_PTR` objects must be rejected as subscription targets for the same reason `retentive` rejects them.
* **Callback-driven sections are designed, not built.** Intended shape: the VM registers one route with `sys_callbacks`, events land on a VM-owned queue drained by the supervisor at a cycle boundary, and typed event blocks publish them. This keeps a single permanent registration, so a program reload swaps only an internal table and cannot leave a dangling callback into a freed arena.
* **NVS retention is not built.** Design: periodic batch flush plus flush-on-shutdown, keyed by **name not id** (ids shift between firmware builds), leaves only.
* An unwired input pin is a NULL accessor slot, spelled `VM_BLOCK_NO_ID` on the wire — the block falls back to a constant it keeps in `custom_data`. There is deliberately no parallel "connected" bitmask: two encodings of the same fact can disagree.

---

## API Summary

**Construction** — `vm_obj_create()`, `vm_obj_shape()`, `vm_obj_init()`, `vm_accessor_create()`, `vm_accessor_set_literal()`, `vm_accessor_set_ref()`, `vm_accessor_set_name()`, `vm_accessor_cache_build()`, `vm_block_create()`

**Dynamic objects** — `vm_obj_dyn_create()`, `vm_obj_dyn_retain()`, `vm_obj_dyn_release()`, `vm_obj_dyn_reset()`, `vm_obj_dyn_id()`, `vm_obj_dyn_get()`, `vm_obj_is_dynamic()`
**Header helpers** — `vm_obj_total_size()`, `vm_obj_payload_size()`, `vm_obj_type_size()`, `vm_type_width()`, `vm_obj_items_cnt()`, `vm_obj_tag()`, `vm_obj_payload()`
**Store** — `vm_store_open()`, `vm_store_alloc()`, `vm_store_get()`, `vm_store_reset()`, `vm_store_used()`, `vm_store_capacity()`
**Access** — `vm_obj_by_id()`, `vm_accessor_by_id()`, `vm_obj_get_payload()`, `vm_get_obj()`, `vm_obj_set_scalar()`, `vm_obj_set_scalar_direct()`, `vm_obj_copy_content()`, `vm_obj_link()`, `vm_obj_link_direct()`, `vm_obj_clear_quiet()`, `vm_payload_at()`, `vm_obj_as_payload()`, `vm_payload_read()`
**User-authored / remote writes** (check `usr_mutable` as well as `mutable`) — `vm_obj_set_scalar_usr()`, `vm_obj_set_scalar_direct_usr()`, `vm_obj_copy_content_usr()`
**Macros** — `VM_OBJ_GET_VAL()`, `VM_OBJ_SET_VAL()`, `VM_OBJ_SET_VAL_AT()`, `VM_OBJ_SET_VAL_USR()`, `VM_OBJ_SET_VAL_AT_USR()`, `VM_PAYLOAD_GET_VAL()`, `VM_VAL_CAST_TO()`
**Blocks** — `vm_block_get_in()`, `vm_block_get_out()`, `vm_block_is_enabled()`, `vm_block_triggered()`, `vm_block_input_fresh()`, `vm_block_set_ENO()`, `vm_block_span()`, `vm_block_claim_span()`, `vm_block_inputs()`, `vm_block_outputs()`, `vm_block_en_list()`, `vm_block_custom_data()`, `vm_block_size()`, `IF_BLOCK_ENABLED()`, `IF_BLOCK_TRIGGERED()`, `BLOCK_CALL()`
**Dispatch** — `vm_block_fn_for()`, `vm_exec_check_block_type()`, `vm_exec_use_blocks()`
**Sections** — `vm_section_create()`, `vm_section_by_id()`, `vm_section_count()`
**Events** — `vm_event_post()`, `vm_event_count()`, `vm_event_at()`, `vm_event_drain()`, `vm_event_take_overflow()`, `vm_event_reset()`
**Execution** — `vm_exec_start()`, `vm_exec_stop()`, `vm_exec_pass()`, `vm_exec_run_range()`, `vm_exec_set_mode()`, `vm_exec_mode()`, `vm_exec_pass_count()`, `vm_exec_last_pass_us()`, `vm_exec_set_sample_hook()`, `vm_exec_reset()`, `vm_clock_us()`, `vm_now_ms()`
**Loading** — `vm_loader_reset()`, `vm_loader_open()`, `vm_loader_add_obj()`, `vm_loader_set_data()`, `vm_loader_add_accessor()`, `vm_loader_add_block()`, `vm_loader_add_section()`, `vm_loader_state()`, `vm_loader_used()`

---

## Error Ownership

Owners `0xA900`–`0xA908` (`SYS_VM_OWNER_MAP` in `core/errors/sys_error_vm.h`): `OWNER_VM_BASE`, `OWNER_VM_STORE`, `OWNER_VM_OBJ`, `OWNER_VM_ACCESSOR`, `OWNER_VM_BLOCK`, `OWNER_VM_CODE`, `OWNER_VM_LOADER`, `OWNER_DEC_VM_LOADER`, `OWNER_VM_EXEC`.

Tags (`SYS_ERROR_VM_MAP`) cover allocation (`ERR_VM_ALLOC_EXHAUSTED`), resolution (`ERR_VM_ACCESSOR_UNKNOWN_ID`, `_OOB`, `_TYPE_MISMATCH`, `_NULL_OBJ`, `_DEPTH_EXCEEDED`, `_INDEX_FAILED`, `_NAME_NOT_FOUND`, `_NOT_MUTABLE`), blocks (`ERR_VM_BLOCK_INPUT_UNRESOLVED`, `_PIN_MISSING`, `_PIN_UNLINKED`, `_FAILED`), construction (`ERR_VM_OBJ_BAD_TYPE`, `_EMPTY`, `_TOO_LARGE`, `_NAME_TOO_LONG`, `_RETENTIVE_PTR`, `_COPY_MISMATCH`), loading (`ERR_VM_LOAD_BAD_STATE`, `_TOO_BIG`, `_DATA_RANGE`, `_SHORT_RECORD`), accessors (`ERR_VM_ACC_INDEX_OOB`, `ERR_VM_ACC_BAD_KIND`), blocks (`ERR_VM_BLK_BAD_SHAPE`, `ERR_VM_BLK_BAD_REF`) and the registries (`ERR_VM_REG_OOB`, `ERR_VM_REG_DUP` — one pair for all four id spaces, with `kind` naming which).

Execution adds its own group: the palette (`ERR_VM_BLK_UNKNOWN_TYPE`), sections (`ERR_VM_SEC_BAD_RANGE`, `ERR_VM_SEC_OVERLAP`), the supervisor (`ERR_VM_EXEC_BLOCK_HUNG`, `ERR_VM_EXEC_SPAN_DEPTH`, `ERR_VM_EXEC_BAD_SPAN`), events (`ERR_VM_EVENT_OVERFLOW`) and the user-write guard (`ERR_VM_OBJ_NOT_USR_MUTABLE`).

`sys_error_vm.h` also supplies `SE_*_OWNED` variants (`SE_EMIT_ERR_OWNED`, `SE_ERR_NEW_OWNED`, `SE_RET_ERR_OWNED`, `SE_WRAP_ERR_OWNED`, `SE_CHECK_NOT_NULL_OWNED`, `SE_CHECK_IF_ALLOCATED_OWNED`) that take the owner explicitly, for header-only code pulled into translation units with their own ambient `OWNER`.

Because `sys_errors` aggregates this map, it reaches `core/errors/sys_error_vm.h` through a raw include path rather than `REQUIRES VM` — VM already requires `sys_errors`, so a component dependency back would be circular.
