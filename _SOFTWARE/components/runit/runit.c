#include "runit.h"
#include <esp_log.h>
#include <string.h>
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "runit_board_devices.h"
#include "sys_actions.h"
#include "sys_callbacks.h"
#include "sys_interface.h"
#include "vm_bench.h"
#include "vm_selftest.h"
#include "vm_sub.h"

static const char* TAG = "runit_app";

static err_h runit_sub_ble_sender(const uint8_t* data, size_t len) {
  return sys_ble_char_send(SYS_BLE_CHR_RUNIT_TX, PACKET_HEADER_TX, data, len, true);
}

#if RUNIT_SKIP_DEVICE_INIT
/* Stands in for runit_at_boot so action 0 still resolves -- see the switch's
   comment in runit_board_cfg.h for why it is bound rather than skipped. */
static err_h runit_at_boot_disabled(void* arg) {
  (void)arg;
  ESP_LOGW(TAG, "device init SKIPPED (RUNIT_SKIP_DEVICE_INIT)");
  return NULL;
}
#endif

static const sys_error_cfg_t s_runit_error_cfg = {
    .global_level = ESP_LOG_INFO,
    .logs =
        {
            .mirror_on_serial = true,
            .ble_enable = true,
            .char_uuid = SYS_BLE_CHR_RUNIT_LOGS,
            .tx_header = PACKET_HEADER_LOGS,
        },
    .errors =
        {
            .serial_trace = true,
            .ble_enable = true,
            .char_uuid = SYS_BLE_CHR_RUNIT_LOGS,
            .tx_header = PACKET_HEADER_ERRORS,
            .packet_max = SE_ERR_PACKET_MAX,
        },
};

void runit_start(void) {
  SE_init();
  SE_ORIGIN_CALL(sys_start_i2c());
  SE_ORIGIN_CALL(sys_power_static_config());
  SE_ORIGIN_CALL(sys_ble_static_config());
  SE_ORIGIN_CALL(SE_configure(&s_runit_error_cfg));
  // Must come before sys_actions_init(): the boot action (id 0) installs
  // devices that can arm interrupts (e.g. ads7128's ALERT pin) whose ISRs
  // queue events via sys_callback_trigger() - sys_cb_task needs to already be
  // running to drain that queue, or every queued event sits forever unread.
  SE_ORIGIN_CALL(sys_callbacks_init());
  SE_ORIGIN_CALL(sys_interface_init());
  // Must come before sys_actions_init(): the boot action (id 0) needs its
  // static function bound before init's unconditional invoke(0) runs.
#if RUNIT_SKIP_DEVICE_INIT
  SE_ORIGIN_CALL(sys_actions_bind_static(0, runit_at_boot_disabled, NULL));
#else
  SE_ORIGIN_CALL(sys_actions_bind_static(0, runit_at_boot, NULL));
#endif
  // Must come before sys_interface_bind_ble_rx(): class registration and the
  // recording tap are boot-only, not safe against a running RX pump.
  SE_ORIGIN_CALL(sys_actions_init());
  SE_ORIGIN_CALL(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, RUNIT_BLE_RX_FRAME_MAX));
  SE_ORIGIN_CALL(vm_sub_init());
  vm_sub_set_sender(runit_sub_ble_sender);
  ESP_LOGI(TAG, "runIT boot sequence complete");
#if RUNIT_ENABLE_VM_SELFTEST
  /* After sys_interface_init() so class 0x04 is registered -- the test
     injects real frames through sys_interface_decode() rather than calling
     the loader directly. */
  vm_selftest_run();
#endif
#if RUNIT_ENABLE_VM_BENCH
  vm_bench_run();
#endif
}
