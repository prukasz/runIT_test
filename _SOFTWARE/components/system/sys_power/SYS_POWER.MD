# System Power Manager (sys_power)

IMPORTANT: If you modify the source files in this component (sys_power.h or sys_power.c), you must update this documentation to reflect the changes.

## Overview
The `sys_power` component implements a unified subsystem for regulating, monitoring, and distributing power across the runIT system. It manages voltage regulators (VREG), power monitors (telemetry), and USB Power Delivery (USB-PD) sinks. It features a centralized power budget tracker that limits voltage and current allocation according to physical hardware limitations and input source capacity, integrated with the central `callbacks` routing pipeline.

### Directory Map
```
components/system/sys_power/
├── CMakeLists.txt
├── SYS_POWER.MD       # This documentation
├── sys_power.c        # Power budget allocation, registry management, and API routing
└── include/
    ├── sys_power.h    # Feature contracts, event definitions, callback macros, and API declarations
    └── sys_power_status_codes.h # Power owner map and status code definitions
```

---

## Architecture & Concept

All power hardware modules implement one or more feature contracts. Submodules register themselves under a unique `device_id` during installation, binding their driver context handle and implementation vtables.

### Power Budget Management
To prevent hardware overloading and source brownouts, the manager implements a power allocation budget tracker:
* **Capacity Tracking**: Tracks maximum source voltage and current.
* **Safety Thresholds**: Configurable at startup via `sys_power_set_limits(max_mv, max_ma, max_mw)` (defined in `runit_board_cfg.h`). Accessible via getter macro expressions:
  * Max voltage limit: `SYS_POWER_LIMIT_MV` -> `sys_power_get_limit_mv()`
  * Max current limit: `SYS_POWER_LIMIT_MA` -> `sys_power_get_limit_ma()`
  * Max total power budget: `SYS_POWER_BUDGET_MW` -> `sys_power_get_budget_mw()`
* **Dynamic Auditing**: When requesting a voltage or current change on a VREG, the system validates the request against the remaining power budget before forwarding the command to the hardware.

---

## Core Data Structures & Enums

### Power Telemetry Events (sys_power_events_e)
Events reported by power monitoring or protection systems.
```c
typedef enum sys_power_events_e {
  SYS_PWR_EVENT_NONE         = 0,
  SYS_PWR_EVENT_OVP          = 1, // Over-Voltage Protection
  SYS_PWR_EVENT_UVP          = 2, // Under-Voltage Protection
  SYS_PWR_EVENT_SPC          = 3, // Short Circuit Protection
  SYS_PWR_EVENT_OCP_WARNING  = 4, // Over-Current Warning
  SYS_PWR_EVENT_OCP_CRITICAL = 5, // Over-Current Critical
  SYS_PWR_EVENT_OTP          = 6, // Over-Temperature Protection
} sys_power_events_e;
```

---

## Feature Contract Interfaces (Vtables)

### Voltage Regulator Contract (sys_power_vreg_contract)
Controls programmable power supplies, buck-boost converters, or LDOs.
```c
typedef struct sys_power_vreg_contract {
  status_rep_t (*set_enable)(void* device_handle, bool state);
  status_rep_t (*set_voltage)(void* device_handle, uint32_t voltage_mV);
  status_rep_t (*set_current)(void* device_handle, uint32_t current_mA);
  status_rep_t (*add_callback)(void* device_handle, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);
} sys_power_vreg_contract;
```

### Power Monitor Contract (sys_power_monitor_contract)
Reads real-time telemetry and registers hardware alerts.
```c
typedef struct sys_power_monitor_contract {
  status_rep_t (*get_voltage)(void* device_handle, uint8_t channel, int32_t* out_mV);
  status_rep_t (*get_current)(void* device_handle, uint8_t channel, int32_t* out_mA);
  status_rep_t (*add_callback)(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);
} sys_power_monitor_contract;
```

### USB Power Delivery Contract (sys_power_usb_pd_contract)
Interacts with USB-PD controller sinks to request source contracts.
```c
typedef struct sys_power_usb_pd_contract {
  status_rep_t (*set_settings)(void* device_handle, uint32_t voltage_mV, uint32_t current_mA);
  status_rep_t (*list_options)(void* device_handle);
  status_rep_t (*get_limits)(void* device_handle, uint32_t* out_mV, uint32_t* out_mA);
} sys_power_usb_pd_contract;
```

---

## Utility Macros

### Callback Trigger Macro
* **`SYS_PWR_CB(_ctx, _chan, _event, _value, _route_mask, _action_mask)`**:
  Constructs a `cb_event_t` with `CALLBACK_PWR` type, sets `pwr` payload parameters plus `head.route_mask`/`head.action_id`, and triggers `sys_callback_trigger`. `_action_mask` is a bitmask of `sys_actions` ids (bit *i* selects action id *i*) carried on `cb_event_t.head.action_id` — not yet dispatched to `sys_actions_invoke()` by `sys_cb_task` (see [[SYS_CALLBACKS.MD]]).

---

## Manager API Functions

### Declarative Contract Binding
* Device adapters publish their contracts declaratively via `sys_device_class_t.contracts[SYS_DEVICE_CONTRACT_POWER_*]` when installed via `sys_device_install_cfg()`.

### Power Budget API
* **`sys_power_set_limits(max_mv, max_ma, max_mw)`**: Sets system safety limits. This function can only be called once at startup (secure write-once constructor).
* **`sys_power_budget_update_source(max_mV, max_mA)`**: Configures power supply limitations based on currently attached power adapter capacity.

### Voltage Regulator (VREG) APIs
* **`sys_vreg_set_enable(device_id, state)`**: Enables/disables target power rail.
* **`sys_vreg_set_voltage(device_id, voltage_mV)`**: Updates regulator output voltage (subject to budget audit).
* **`sys_vreg_set_current(device_id, current_mA)`**: Configures regulator current limit threshold.
* **`sys_vreg_add_callback(device_id, on_event, route_mask, action_mask)`**: Subscribes a callback route mask (and a `sys_actions` action_mask, carried through on the fired `cb_event_t.head.action_id`) to hardware protection flags.

### Telemetry (Monitor) APIs
* **`sys_power_monitor_get_voltage(device_id, channel, out_mV)`**: Fetches channel voltage.
* **`sys_power_monitor_get_current(device_id, channel, out_mA)`**: Fetches channel current.
* **`sys_power_monitor_add_callback(device_id, channel, trigger_value, on_event, route_mask, action_mask)`**: Sets telemetry threshold warnings, and the `sys_actions` action_mask to carry on the fired event.

### USB Power Delivery (USB-PD) APIs
* **`sys_power_usb_pd_set(device_id, voltage_mV, current_mA)`**: Initiates power negotiation request.
* **`sys_power_usb_pd_list(device_id)`**: Queries and lists source capabilities advertised by USB-PD host.
* **`sys_power_usb_pd_get_limits(device_id, out_mV, out_mA)`**: Fetches negotiated voltage/current bounds.
