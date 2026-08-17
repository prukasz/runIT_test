---
name: runit
description: Technical architecture guidelines, device driver/adapter patterns, error handling conventions, submodule navigation, and build instructions for the runIT firmware codebase under ESP-IDF 6.0.
---

# runIT Development Skill & Architecture Guide

## Read Before Overview

`runIT` is a combined hardware and software project aimed at simplifying peripheral module integration.

- **Hardware**: Carrier PCB boards with onboard components (voltage regulators, ADCs, GPIO expanders, H-bridges, hardware safety mechanisms) and direct ESP output. Board variations support static preinstallation of onboard components.
- **Software System**: Provides a unified API, rich error handling, and ready-to-go connection methods. Plug-and-play device integration uses system stencils so devices are registered dynamically at runtime and accessed by ID via APIs (`sys_io`, `sys_power`, etc.).
- **Virtual Machine (VM)**: Operates on top of the System API using a pre-compiled, low-dynamic-allocation block-based scripting language (combining PLC logic and Node flow concepts) uploaded wirelessly. Includes high-level abstraction blocks (servos, LED strips, DC motors, sensors), real-time script editing, and a wireless **Remote Control** mode.
- **Safety & Error Telemetry**: The System layer manages critical safety events (short circuits, hardware failures). Callbacks can be routed (e.g. to VM components) for pseudo-ISR non-polling behavior. Custom error traces flow seamlessly into VM contexts, and errors/logs are transmitted wirelessly for user-friendly client-side identification.

---

## Submodule Discovery & Keyword Routing Rules

### Documentation Lookup Protocol
When task prompts mention specific system concepts or keywords without explicit file attachments, follow this strict three-step lookup sequence:
1. **Step 1 (`.MD` Overview)**: View the submodule's root `.MD` file for architectural concepts, data structures, and public macro/function reference.
2. **Step 2 (`.h` Headers)**: View public header files (`include/*.h`) to check exact function signatures, enums, structs, and status codes.
3. **Step 3 (`.c` Implementation)**: View `.c` source files only when low-level execution logic, hardware register sequence, or bug debugging is required.

---

### System Core Submodule Mapping

| Trigger Keywords | Target Submodule | Primary Documentation | Key Headers / Files |
| :--- | :--- | :--- | :--- |
| `device`, `adapter`, `contract`, `ops`, `lifecycle`, `install`, `uninstall` | `components/system/sys_device` | [SYS_DEVICE.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_device/SYS_DEVICE.MD) | [sys_device.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_device/include/sys_device.h) |
| `io`, `pin`, `adc`, `dac`, `gpio`, `pwm`, `level`, `toggle`, `voltage` | `components/system/sys_io` | [SYS_IO.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_io/SYS_IO.MD) | [sys_io.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_io/include/sys_io.h), [shared_io_types.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_io/include/shared_io_types.h) |
| `power`, `regulator`, `voltage`, `current`, `limit`, `budget`, `monitor` | `components/system/sys_power` | [SYS_POWER.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_power/SYS_POWER.MD) | [sys_power.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_power/include/sys_power.h) |
| `i2c`, `bus`, `transmit`, `receive`, `master` | `components/system/sys_i2c` | [SYS_I2C.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_i2c/SYS_I2C.MD) | [sys_i2c.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_i2c/include/sys_i2c.h) |
| `ble`, `bluetooth`, `nimble`, `gatt`, `characteristic`, `service` | `components/system/ble` | [SYS_BLE.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/ble/SYS_BLE.MD) | [sys_ble.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/ble/include/sys_ble.h) |
| `buffer`, `ringbuf`, `queue`, `storage` | `components/system/sys_buffers` | [SYS_BUFFERS.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_buffers/SYS_BUFFERS.MD) | [sys_buffers.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_buffers/include/sys_buffers.h) |
| `callback`, `route`, `event`, `isr`, `trampoline` | `components/system/callbacks` | [SYS_CALLBACKS.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/callbacks/SYS_CALLBACKS.MD) | [sys_callbacks.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/callbacks/include/sys_callbacks.h) |
| `interface`, `route`, `class header`, `dispatch`, `packet`, `protocol` | `components/system/sys_interface` | [SYS_INTERFACE.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_interface/SYS_INTERFACE.MD) | [sys_interface.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_interface/include/sys_interface.h) |
| `codec`, `decoder`, `encoder`, `payload`, `wire format`, `HEADER_packet_` | `components/codecs` | [CODECS.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/codecs/CODECS.MD) | [dec_sys_contracts.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/codecs/decoders/dec_sys_contracts.h), [enc_sys_errors.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/codecs/encoders/enc_sys_errors.h) |
| `error`, `status`, `owner`, `tag`, `trace`, `SE_`, `log level`, `telemetry` | `components/sys_errors` | [SYS_ERRORS.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/sys_errors/SYS_ERRORS.MD) | [sys_error.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/sys_errors/include/sys_error.h) |
| `action`, `record`, `replay`, `invoke`, `macro`, `interceptor`, `tap`, `preconfiguration`, `boot action`, `static action`, `bind static`, `nvs`, `state`, `mode`, `frozen`, `suspend`, `reset`, `hard-reset`, `emergency` | `components/system/sys_actions` | [SYS_ACTIONS.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_actions/SYS_ACTIONS.MD) | [sys_actions.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_actions/include/sys_actions.h) |
| `vm`, `object`, `accessor`, `payload`, `arena`, `block`, `pin`, `EN`, `ENO`, `supervisor`, `section`, `scan cycle`, `pass`, `palette`, `block_type`, `activation`, `trigger_mask`, `event queue`, `program upload`, `loader`, `vm_obj_`, `vm_loader_`, `vm_exec_`, `vm_block_fn` | `components/VM` | [VM.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/VM.MD) (built), [VM_EXEC.MD](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/VM_EXEC.MD) (execution model & user-facing target) | [vm_store.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/store/vm_store.h), [vm_obj.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/obj/vm_obj.h), [vm_obj_access.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/obj/vm_obj_access.h), [vm_loader.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/loader/vm_loader.h), [vm_block.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/block/vm_block.h), [vm_exec.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/exec/vm_exec.h), [vm_section.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/exec/vm_section.h), [vm_event.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/core/exec/vm_event.h) |<br>`core/` is grouped by layer (`errors`, `store`, `obj`, `block`, `exec`, `loader`) but **all includes are by bare filename** — every group is on the include path.<br>**A new block type** is one `.c` in [components/VM/blocks/](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/VM/blocks/): a `void fn(vm_block_h)`, plus one line in `g_vm_blocks[]` in `vm_blocks_table.c` — a `const` array in flash indexed by `block_type`, and that is the whole palette. Start by copying the placeholder in `vm_blocks_dummy.c` nearest the shape you want. The block decides its own activation (`vm_block_triggered()` / `vm_block_is_enabled()`); its whole else-branch is `vm_block_set_ENO(b, false)` — outputs stand, and the supervisor's end-of-pass `upd` sweep withdraws their freshness. |
| `board`, `config`, `defs`, `start`, `runit`, `selftest`, `benchmark` | `components/runit` | — | [runit_board_cfg.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/runit/runit_board_cfg.h), [runit.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/runit/runit.c), [vm_selftest.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/runit/vm_selftest.c), [vm_bench.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/runit/vm_bench.c) |

---

### Hardware Device Drivers & Adapters Mapping

| Device Chip / Function | Directory Path | Adapter File | Driver Header / File |
| :--- | :--- | :--- | :--- |
| **ESP32 Native GPIO & ADC** | `components/devices/device_gpio_esp` | [adapter_gpio_esp.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_gpio_esp/adapter_gpio_esp.c) | [esp_adc_config.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_gpio_esp/esp_adc_config.c) |
| **PCA9685 (PWM Expander)** | `components/devices/device_pca9685` | [adapter_pca9685.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_pca9685/adapter_pca9685.c) | [driver_pca9685.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_pca9685/driver_pca9685.h) |
| **TCA6424A (24-bit GPIO Expander)** | `components/devices/device_tca6424a` | [adapter_tca6424a.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_tca6424a/adapter_tca6424a.c) | [driver_tca6424a.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_tca6424a/driver_tca6424a.h) |
| **TPS55289 (Buck-Boost Converter, Voltage Regulator)** | `components/devices/device_tps55289` | [adapter_tps55289.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_tps55289/adapter_tps55289.c) | [driver_tps55289.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_tps55289/driver_tps55289.h) |
| **INA3221 (3-Ch Power Monitor)** | `components/devices/device_ina3221` | [adapter_ina3221.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ina3221/adapter_ina3221.c) | [driver_ina3221.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ina3221/driver_ina3221.h) |
| **AP33772S (USB-PD Controller)** | `components/devices/device_ap33772s` | [adapter_ap33772s.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ap33772s/adapter_ap33772s.c) | [driver_ap33772s.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ap33772s/driver_ap33772s.h) |
| **DAC53202 (Dual DAC)** | `components/devices/device_dac53202` | [adapter_dac53202.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_dac53202/adapter_dac53202.c) | [driver_dac53202.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_dac53202/driver_dac53202.h) |
| **ADS7128 (8-Channel ADC Expander)** | `components/devices/device_ads7128` | [adapter_ads7128.c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ads7128/adapter_ads7128.c) | [driver_ads7128.h](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/devices/device_ads7128/driver_ads7128.h) |


---

## Code Flow & Architectural Guidelines

### 1. System Module & Error Abstraction Rules
- **No Large Assumptions**: Never make large architectural assumptions; present options and ask the user for confirmation when requirements are ambiguous.
- **Hardware Error Abstraction**: Hide vendor and ESP-IDF error codes (`esp_err_t`) from top-level APIs. Translate or wrap all low-level errors into system error handles (`err_h` / `status_rep_t`) using `sys_error` macros.
- **Error Stack Trace Origin (`SE_ORIGIN_CALL`)**: Wrap top-level user entry points and configuration routines (e.g., `runit_start()`) with `SE_ORIGIN_CALL(...)`. Internal nested routines must use `SE_RET_IF_ERR(...)` to propagate errors so line traces are appended cleanly without duplicate origin frames.
- **Function Return Signatures**: Most functions must return `err_h`. For `void` functions, log or handle errors using designated logging macros (e.g., `CHECK_AND_LOG`).
- **Module Owner Tagging**: Every significant source file must define `#define OWNER OWNER_<MODULE_NAME>` at the top. Ensure the owner enum/macro is added to the system owner map and matches the module purpose.
- **Static Allocation by Default**: System components (queues, tasks, semaphores, mutexes, event groups, ring/stream/message buffers, timers) should be created static, not dynamically, via the `utils` component's `R_*_DEFINE` / `R_TASK_START(...)` macros (`components/utils/utils.h`) instead of raw `xQueueCreate`, `xTaskCreate`, etc. This removes heap dependence and the allocation-failure error path entirely for that object — one less thing to check, one less way to fail at runtime. `R_*_DEFINE` macros construct at load time (via `__attribute__((constructor))`), so init functions don't need to guard their creation, only start-if-not-already-started logic for tasks (`R_TASK_START` is not auto-invoked). See `sys_error.c`'s `s_err_queue` (`R_QUEUE_DEFINE`) and `s_err_handler_task_handle` (`R_TASK_DEFINE`/`R_TASK_START`) for the pattern.

### 2. Device Adapter & Driver Development Rules
- **Comprehensive Feature Set**: Unless explicitly restricted, implement all chip functionalities supported by the hardware.
- **System Macro Priority**: Use system-provided macros (`SYS_DEV_CHECK_DRIVER_CALL`, `SYS_DEV_GET_ADAPTER_CONTEXT`, `IF_PIN`) as default solutions instead of writing raw boilerplate.
- **Adapter / Driver Separation**: Keep low-level register and bus logic in `driver_xxx.c/h`. Bind hardware capabilities to system contracts in `adapter_xxx.c`.
- **System Bus Communication**: In drivers, use `sys_i2c` bus functions (`sys_i2c_master_transmit`, `sys_i2c_master_transmit_receive`) instead of raw IDF calls.
- **Inter-Device Dependency Handling**: When a device depends on another peripheral (e.g., enable/reset/interrupt pins on a GPIO expander), accept `sys_io_pin_ref_t` in the config struct. Wrap dependency calls in the adapter.
- **Single Creation Function**: Expose ONLY `d_xxx_create(const d_xxx_cfg_t* config)`. All runtime hardware access MUST be bound through system contract function tables (`sys_device_install`). Extend contract structs if a new feature type is needed.

### 3. Documentation & Skill Maintenance
- **Self-Updating Skill File**: Update `SKILL.md` whenever structural edits occur (new component directories, submodules, or device drivers).
- **Automatic Submodule Documentation Sync**: Whenever modifying component source files (`.h`, `.c`), automatically update the corresponding component `.MD` files (e.g. `SYS_IO.MD`, `SYS_DEVICE.MD`, `SYS_CALLBACKS.MD` and others) to reflect API signatures, struct definitions, macros, function declarations, or subsystem behaviors. Do not require the user to ask — keep docs and code in sync proactively.
- **Missing Submodule Handling**: If an unlisted C component is encountered, ask the user for its purpose, create its missing `.MD` documentation file, and document its main API usage examples.
- **User Guidelines Persistence**: Append any user corrections or new architectural instructions to `SKILL.md` immediately.
- **Ask Before Guessing**: Never guess the purpose of unknown registers, hardware logic, or schemas; ask the user for clarification and record the answer.
- **Doxygen Header Style**: Format header and macro comments using Doxygen syntax (`@brief`, `@param`, `@return`), including minimal usage examples for new APIs.
- **User Comment Ownership**: Never edit or rewrite a comment the user personally wrote/introduced (per section/block) — leave that block exactly as they wrote it, even if it later looks stale after a related code change. Adding a brand-new comment of your own is fine, but keep it visually separate from the user's block rather than folding into it. Prefer one large `/* ... */` block comment over scattering many `//` line comments when adding your own explanation.





## 4. Build & Environment Verification

### Verified Build Command
Run this command from the project root (`_SOFTWARE`), directly in PowerShell — do NOT wrap it in a nested `powershell -Command "..."` call. `export.ps1` writes its activation banner in a way that a nested shell misclassifies as a `NativeCommandError`, causing a spurious non-zero exit code even though the build succeeds. Do not pipe/redirect its stderr (e.g. `2>&1`) either, for the same reason.
```powershell
. C:\esp\v6.0.1\esp-idf\export.ps1; idf.py build
```

### Flash & Monitor Command
```powershell
. C:\esp\v6.0.1\esp-idf\export.ps1; idf.py -p COM<X> flash monitor
```
