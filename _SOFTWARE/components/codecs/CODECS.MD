# CODECS — Packet Decoder / Encoder Tables

`codecs` is a **header-only** component holding the packet tables that [[SYS_INTERFACE.MD]] routes to. It has no source files and produces no library — `idf_component_register()` only exports `decoders/` and `encoders/` as include directories.

| Directory | Contents |
| :--- | :--- |
| `decoders/` | Inbound packet tables — one header per class byte. |
| `encoders/` | Outbound packet builders — one header per outbound stream. |

The split of responsibility: `sys_interface` owns the **class byte** (`0xXX`) and the transport plumbing; a codec header owns its **packet bytes** (`0xYY`), payload layouts, and the mapping to `sys_*` API calls.

Encoders are the mirror image: they take an in-RAM structure and *fill a caller-supplied buffer*. They never transmit — pushing the buffer into a TX ring (`sys_ble_char_send()`) is the caller's job. They also carry **no class byte**: an outbound stream is already identified by the TX slot header byte that `sys_buff_pop_framed()` prepends (see `sys_ble_char_assign_tx_buffer()`).

---

## `decoders/dec_sys_contracts.h` — class `0x01`

Maps packet bytes onto the system contract APIs (`sys_device`, `sys_io`, `sys_power`). Everything is `static inline` in one header, so adding a packet is a single-file change with no build-system edits.

Each entry is three pieces:

```c
#define HEADER_packet_sys_io_toggle_t 0x24          // 1. packet byte
typedef struct __packed {                            // 2. payload layout
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_toggle_t;

static inline err_h decoder_packet_sys_io_toggle_t(packet_sys_io_toggle_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "toggling io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_toggle(packet->device_id, packet->pin);
}
```

...plus a row in the central `X`-macro list:

```c
#define SYS_CONTRACTS_PACKET_LIST(X) \
  ...                                 \
  X(HEADER_packet_sys_io_toggle_t, packet_sys_io_toggle_t, decoder_packet_sys_io_toggle_t)
```

`SYS_CONTRACTS_PACKET_LIST` expands through `SYS_CONTRACTS_DECODE_CASE` into the `switch` inside `dec_sys_contracts_decode()`, which is what `sys_interface_init()` registers for class `0x01`. Each case declares the payload struct on the stack, fills it via `convert_to_packet()` (length-checked), and calls the decoder.

Every decoder logs what it is about to do at `ESP_LOGI` under the tag `dec_sys_contracts`. Getter packets log the *result* on a second line, since the value has nowhere else to go until an encoder path exists.

### Packet Byte Ranges

| Range | Domain | API |
| :--- | :--- | :--- |
| `0x10`–`0x19` | Device lifecycle | `sys_device_uninstall/reset/suspend/resume/freeze/sync` (+ `_all` variants) |
| `0x1A` | Device error handling | `sys_device_set_error_handling` — `device_id, use_error_handler, generate_error_callback, actions[3]`; see [[SYS_DEVICE.MD]]'s "Per-Instance Error Handling" |
| `0x1B`–`0x1F` | *(free)* | |
| `0x20`–`0x2F` | IO | `sys_io_reset/set_mode/set_level/get_level/toggle/get_voltage/set_voltage/set_pwm_frequency/set_pwm_duty` |
| `0x30`–`0x3F` | Power | `sys_power_budget_update_source`, `sys_vreg_set_enable/voltage/current`, `sys_power_monitor_get_voltage/current`, `sys_power_usb_pd_set/list/get_limits` |
| `0x40`–`0x47` | Device installation | `d_gpio_esp_create`, `d_pca9685_create`, `d_tca6424a_create`, `d_tps55289_create`, `d_ina3221_create`, `d_ap33772s_create`, `d_dac53202_create`, `d_ads7128_create` |

Unmapped packet bytes raise `ERR_INTERFACE_UNKNOWN_PACKET` (owner `OWNER_DEC_SYS_CONTRACTS`) and log a warning.

---

## `decoders/dec_sys_device_install.h` — class `0x01`, packets `0x40`–`0x47`

Split out from `dec_sys_contracts.h` into its own file: one packet per device type, each payload that device's own `d_xxx_cfg_t` flattened directly onto the wire in field order. This is what lets a device be attached dynamically (at boot via a [[SYS_ACTIONS.MD]] boot action, or any time after) instead of only through the hardcoded `*_static_config()` calls in `runit.c`.

Still the **same class byte** (`0x01`) and the **same dispatcher** (`dec_sys_contracts_decode()`) as `dec_sys_contracts.h` — `dec_sys_contracts.h` just `#include`s this file and folds its `SYS_CONTRACTS_INSTALL_PACKET_LIST(X)` into its own `SYS_CONTRACTS_PACKET_LIST(X)`, the same way `sys_error_codes.h` folds several modules' maps into one `SYS_ERROR_MAP(X)`. The split exists purely so `dec_sys_contracts.h` doesn't have to carry all 8 device headers' includes; it changes nothing about the wire format. Tagged `OWNER_DEC_SYS_DEVICE_INSTALL` (`0xA612`), logged under `dec_sys_device_install` — distinct from `dec_sys_contracts`'s own tag/owner, since it's a separate file.

**No nested struct fields in any packet payload — scalars only.** `sys_io_pin_ref_t` (inter-device pin dependencies — e.g. a regulator's enable pin, wired through a GPIO expander) is not `__packed` (its `sys_io_mode_e` field is int-sized) and, even if it were, would still be a nested type inside the payload. Every pin reference is instead flattened to three plain fields, named `<name>_device_id`, `<name>_pin`, `<name>_mode`, reassembled into a real `sys_io_pin_ref_t` only in the decoder via `pin_ref_from_wire(device_id, pin, mode)`. This keeps every payload struct walkable as a flat list of `uint8_t`/`uint16_t`/`uint32_t`/`bool` fields — deliberately, so future tooling that generates encoders/decoders from these payloads never has to special-case a nested type.

```c
static inline sys_io_pin_ref_t pin_ref_from_wire(uint8_t device_id, uint8_t pin, uint8_t mode);
```

**An unused pin must be encoded as `pin == SYS_GPIO_NONE`** (`SYS_GPIO_NONE` = `0xFF`) — `device_id`/`mode` are ignored whenever that's true, exactly like `SYS_IO_PIN_NONE` on the C side. Sending all-zero fields means "device 0, pin 0" — a real pin, not "none".

| Packet | Byte | Payload (after `device_id`, `i2c_bus`, `i2c_addr` where present) |
| :--- | :--- | :--- |
| Install `gpio_esp` | `0x40` | *(none — `device_id` only)* |
| Install `pca9685` | `0x41` | `oe_pin_device_id, oe_pin_pin, oe_pin_mode` |
| Install `tca6424a` | `0x42` | `intr_pin_{device_id,pin,mode}, rst_pin_{device_id,pin,mode}` |
| Install `tps55289` | `0x43` | `intr_pin_{device_id,pin,mode}, en_pin_{device_id,pin,mode}` |
| Install `ina3221` | `0x44` | `crit_pin_{device_id,pin,mode}, warn_pin_{device_id,pin,mode}` |
| Install `ap33772s` | `0x45` | `intr_pin_{device_id,pin,mode}` |
| Install `dac53202` | `0x46` | *(none)* |
| Install `ads7128` | `0x47` | `intr_pin_{device_id,pin,mode}; uint32_t vref_mv` |

`i2c_bus` is a single byte (`0` = `SYS_I2C_BUS0`/internal, `1` = `SYS_I2C_BUS1`/external). Example — install a PCA9685 as device 5 on the internal bus at I2C address `0x40`, no OE pin: `01 41 05 00 40 00 FF 00`.

---

## `decoders/dec_sys_actions.h` — class `0x03`

Control-packet table for [[SYS_ACTIONS.MD]]'s record/stop/remove wire protocol. Same shape as `dec_sys_contracts.h` (a `SYS_ACTIONS_PACKET_LIST` X-macro expanded into a switch), just with `sys_actions.h` in place of `sys_io.h`/`sys_device.h`/`sys_power.h` as the thing being wrapped.

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Record | `0x01` | `uint8_t action_id` | `sys_actions_record_start(action_id)` |
| Stop | `0x02` | `uint8_t action_id` | `sys_actions_record_stop(action_id)` (persists to NVS) |
| Remove | `0x03` | `uint8_t action_id` | `sys_actions_remove(action_id)` |
| Execute | `0x04` | `uint8_t action_id` | `sys_actions_invoke(action_id)` — test-only wire exposure, see [[SYS_ACTIONS.MD]] |

`SYS_ACTIONS_CLASS_HEADER` (`0x03`) is defined in `sys_actions.h`, not this file — unlike `SYS_CONTRACTS_CLASS_HEADER` (owned by the codec because it spans three unrelated system components), sys_actions' class is 1:1 with a single component, so the constant lives with that component's own public contract. `dec_sys_actions.h` just includes `sys_actions.h` to use it.

Registered by `sys_actions_init()`, not `sys_interface_init()` — see the boot-ordering note in [[SYS_ACTIONS.MD]]. There is no separate "bind action to state" class anymore — the `sys_states` component that used to own it was folded into `sys_actions` (a hardcoded behavior is now just a bound static function on an action id, see [[SYS_ACTIONS.MD]]); class `0x04` was freed by that change and has since been taken by the VM loader below.

---

## `decoders/dec_vm_loader.h` — class `0x04`

Program-upload table for [[VM.MD]]. **Structurally different from the tables above**: these packets are not fixed-size structs, so there is no `X`-macro packet list and no `convert_to_packet()` copy. `0x42`/`0x43` carry a repeating record list whose entries vary in length (inline name bytes, inline payload bytes), so they are walked with an explicit cursor.

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Reset | `0x40` | — | `vm_loader_reset()` |
| Open | `0x41` | `u16 obj_cnt`, `u16 acc_cnt`, `u16 blk_cnt`, `u32 total_size` | `vm_loader_open()` |
| Add objects | `0x42` | `u8 n`, `n ×` `{u16 id, vm_obj_head_t head, char name[head.d.name_size]}` | `vm_loader_add_obj()` per record |
| Set data | `0x43` | `u8 n`, `n ×` `{u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len]}` | `vm_loader_set_data()` per record |
| Add accessors | `0x44` | `u8 n`, `n ×` `{u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len]}` | `vm_loader_add_accessor()` per record |
| Add block | `0x45` | one block: header, `in_cnt ×` accessor id, `q_cnt ×` object id, `en_cnt ×` accessor id, `custom_len ×` private state | `vm_loader_add_block()` |

`idx_data` holds `idx_count` records of `{u8 kind, payload}` — `LITERAL` a `u32` position, `REF` a `u16` accessor id, `NAME` a `u8` length then that many unterminated bytes. A `REF` target must already exist, which makes reference cycles unconstructable rather than merely depth-capped at resolve time.

`0x45` carries exactly **one** block, unlike everything else here: a block record varies in length four separate ways, so batching would need a length prefix per record and a cursor that can desynchronise. A block names accessors and objects that must already be bound. **Block order is execution order** — the client topologically sorts and uploads in that order.

All multi-byte fields little-endian. Every length is checked against the bytes actually present before use (`DEC_VM_NEED`), raising `ERR_VM_LOAD_SHORT_RECORD` rather than reading past the frame — the cursor walk has no struct-size check to lean on, so this is the only thing standing between a truncated frame and an overread.

`DEC_VM_NEED`'s parameters are named `in_pkt`/`in_off`/`in_len`/`in_need` so plain token substitution cannot collide with the `.packet`/`.need`/`.got` payload designators inside it — the same hazard `SE_CHECK_IN_RANGE` documents in [[SYS_ERRORS.MD]]. Naming them `packet`/`need` expands `.packet = (packet)` into `.0x41 = (0x41)`.

Wire-format rationale (`start_idx` unconditional, `payload_size` in bytes rather than an element count, explicit flags byte rather than the raw header bitfield, child **ids** rather than pointers for `VM_OBJ_PTR`) is documented in [[VM.MD]].

Registered by `sys_interface_init()` alongside `dec_sys_contracts`, not self-registered from the VM: `codecs` already `REQUIRES VM` so the decoder can see `vm_loader.h`, which makes a `VM → codecs` dependency circular. Worth moving into a `vm_init()` once the VM owns one.

---

## `encoders/enc_sys_errors.h` — error chain telemetry

Flattens an `err_h` chain into one self-describing packet so the client can rebuild the whole stack trace and log it there — the firmware never has to spend flash on human-readable strings.

```c
err_h enc_sys_errors_encode_chain(err_h chain, uint8_t* out_buf, size_t out_max, size_t* out_len);
```

### Wire format

```
+--------- packet header (3 B) ---------+
| u8 version | u8 node_count | u8 depth |
+---------------------------------------+
| node[0] | node[1] | ... | node[n-1]   |
+---------------------------------------+

node record:
+----------------+--------------+----------------+-------------+
| u8 payload_len | u16 tag (LE) | u16 owner (LE) | payload ... |
+----------------+--------------+----------------+-------------+
```

| Field | Meaning |
| :--- | :--- |
| `version` | `ENC_SYS_ERRORS_FMT_VERSION` — bumped on any layout change. |
| `node_count` | Node records present in this packet. |
| `depth` | Full length of the walked chain. `depth > node_count` ⇒ truncated (buffer full, or `ENC_SYS_ERRORS_MAX_NODES` = 16 reached). |
| `payload_len` | `SE_get_payload_size(tag)`, repeated on the wire so a client with a stale tag table can still skip an unknown node. |
| `tag` / `owner` | `err_tag_e` / `sys_owner_e` values. |
| `payload` | The node's payload struct copied verbatim. |

Nodes are emitted **outermost first** (the `SE_ORIGIN_CALL` frame), each following record being the previous one's `next_cause`. That pointer is stripped — ordering carries it — and there is deliberately **no index byte**: records are contiguous and `node_count` bounds them, so an index would only restate the loop counter.

### Design notes

* **Encode before anything else.** Every error the function can raise is raised *before* the chain is walked (the "not even one node fit" case is checked after). `SE_*` macros allocate from the same ring the chain lives in, so allocating mid-walk could clobber the nodes being encoded. The handler task in `sys_error.c` follows the same rule: encode → serial trace → send.
* **`ENC_SYS_ERRORS_MAX_NODES`** hard-stops traversal. Error nodes live in a lock-free ring with no free, so a chain held too long can have a `next_cause` overwritten into a cycle.
* **Truncation is not an error.** The walk stops once a record no longer fits but keeps counting `depth`, so the client always learns how much it lost.

### Client side (Python `ctypes`)

Payload structs are **not** `__packed` — they are plain C structs, so mirror them with a default-alignment `ctypes.Structure` (*not* `_pack_ = 1`). The three fixed fields of a node record are little-endian and unaligned: `struct.unpack_from("<BHH", buf, off)`.

Errors: `ERR_NULL_PTR`, `ERR_INTERFACE_ENC_BUF_TOO_SMALL` (`got`/`need`). Owner: `OWNER_ENC_SYS_ERRORS` (`0xA620`).

---

## Adding a New Class

Copy the shape of `dec_sys_contracts.h`:

1. Create `decoders/dec_<name>.h` with a `#define <NAME>_CLASS_HEADER 0xXX`.
2. Set `#undef OWNER` / `#define OWNER OWNER_<NAME>` near the top so `SE_*` macros tag errors correctly, and add that owner to the module's `sys_error_*.h` owner map.
3. Expose one `static inline err_h dec_<name>_decode(const uint8_t* data, size_t len)` — `data[0]` is the packet byte.
4. Register it at boot: `sys_interface_register_class(<NAME>_CLASS_HEADER, dec_<name>_decode, "<name>")`.

Note that a decoder header defines `OWNER` for the rest of the translation unit; any `.c` including it must re-`#define OWNER` afterwards (as `sys_interface.c` does).

**Splitting a class across files.** If one class's packet table grows too large for one header, split it the way `dec_sys_device_install.h` splits off from `dec_sys_contracts.h`: give the split-out file its own `OWNER`/log tag and its own `SOME_NAME_PACKET_LIST(X)`, then have the original file `#include` it and fold that list into its own via `SOME_NAME_PACKET_LIST(X)` as one line inside its own `X`-macro definition (same technique `sys_error_codes.h` uses to combine multiple modules' `SYS_ERROR_*_MAP(X)`s into one `SYS_ERROR_MAP(X)`). The class byte, dispatcher function, and registration call stay in the original file — only packet declarations move.

### VM execution control

`04 48 <command>` is the single execution-control packet; the command is one
byte and no additional payload is accepted. Commands `00` through `08` select
scan mode, execute once, select block mode, execute next, reset to scan start,
select normal mode, pause, resume, and reset/unload respectively. See
`components/VM/VM.MD` for stepping and reset semantics and
`vm_exec_command_e` for the C constants.
