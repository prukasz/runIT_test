#pragma once
#include <stdint.h>
#include "sys_error.h"

typedef enum callback_type_e { CALLBACK_NONE = 0, CALLBACK_IO = 1, CALLBACK_PWR = 2, CALLBACK_BLE = 3, CALLBACK_OWN_FUNC = 4, CALLBACK_MAX } callback_type_e;

typedef struct sys_callback_head_t {
  uint16_t callback_type; /* callback_type_e value */
  uint16_t route_mask;    /* Bitmask for destination routing */
  uint64_t action_id;     /*What action to invoke: bitmask: 0 means none*/
} sys_callback_head_t;

/**
 * @brief Runtime-registered route table slot indices.
 *
 * Bit `i` of `sys_callback_head_t.route_mask` selects route slot `i` in
 * `sys_cb_task`'s dispatch table - callers building a route mask (e.g. the
 * `route_mask` argument of `sys_vreg_add_callback()`, `sys_ble_add_callback()`)
 * OR the bits of every route they want the event delivered to together, using
 * `SYS_CB_ROUTE_BIT()`. Slots are filled at load time by each owning
 * component via `sys_cb_register_route()` (see below) rather than baked into
 * `sys_callbacks.c` - an index nobody registers into is silently skipped, not
 * an error.
 */
#include <sdkconfig.h>
#define SYS_CB_ROUTE_IO 0   /* sys_io's own callback-event handler */
#define SYS_CB_ROUTE_BLE 1  /* ble's own callback-event handler */
#define SYS_CB_ROUTE_PWR 2  /* sys_power's own callback-event handler */
#define SYS_CB_ROUTE_WIFI 3 /* WiFi-specific handling - not implemented yet */
/* The VM's per-scan-cycle event buffer. Unlike the routes above, this one does
   not *handle* the event - it parks it for the length of one scan cycle so
   blocks can query it, then drops it. See [[VM_EXEC.MD]]. */
#define SYS_CB_ROUTE_VM 4

#define SYS_CB_ROUTE_BIT(idx) (1u << (idx))

typedef struct io_event_t {
  uint8_t device_id;
  uint8_t pin_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // ADC reading or digital state
} io_event_t;

typedef struct pwr_event_t {
  uint8_t device_id;
  uint8_t channel_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // Current or voltage reading
} pwr_event_t;

typedef struct ble_event_t {
  uint32_t event;
  int32_t value;
} ble_event_t;

struct cb_event_t;

typedef struct own_func_t {
  err_h (*own_func)(void* device_handle, struct cb_event_t* event);
  void* device_handle;
} own_func_t;

typedef own_func_t own_funct_t;

typedef struct cb_event_t {
  sys_callback_head_t head;
  union {
    io_event_t io;
    pwr_event_t pwr;
    ble_event_t ble;
    own_func_t own_func;
  } event;
} cb_event_t;

typedef void (*sys_cb_route_func_t)(const cb_event_t* event);

err_h sys_callbacks_init(void);
err_h sys_callback_trigger(const cb_event_t* event);

/**
 * @brief Register fn as the handler for route slot route_idx.
 *
 * Called once by each domain component (sys_io, sys_power, ble) at load time
 * (a `__attribute__((constructor))` function - see the [[runit]] skill's
 * static-allocation/load-time-construction convention) to plug its own
 * callback-event handler into sys_cb_task's dispatch table, without
 * sys_callbacks needing to depend on that component. Overwrites any handler
 * already registered at route_idx.
 *
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if route_idx is out of range.
 */
err_h sys_cb_register_route(uint8_t route_idx, sys_cb_route_func_t fn);

#define SYS_CB_OWN(own_func_struct)                   \
  do {                                                \
    cb_event_t __cb_evt = {                           \
        .head = {.callback_type = CALLBACK_OWN_FUNC}, \
        .event.own_func = (own_func_struct),          \
    };                                                \
    sys_callback_trigger(&__cb_evt);                  \
  } while (0)
