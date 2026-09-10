#pragma once
/******************************************************
Board specific hardware configurations
!Board rev. 1.0

!Note

Pin configs and device id / adresses shall not be changed
****************************************************** */

#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_interface.h"
#include "sys_power.h"

/* ---------------------------------------------------------------------------
   Bring-up switches. Both default off; flip for bench testing, not for a
   shipping build.

   RUNIT_SKIP_DEVICE_INIT skips binding runit_at_boot to action 0, so no I2C
   device is probed or installed. Action 0 still gets a no-op static function
   bound in its place -- leaving it unbound would make sys_actions_init()'s
   unconditional invoke(0) report ERR_ACTION_NOT_FOUND at every boot. Useful
   when testing logic that doesn't need hardware, or on a board where a
   missing//faulty peripheral would otherwise stall boot.

   RUNIT_ENABLE_VM_SELFTEST runs vm_selftest_run() at the end of boot -- see
   [[vm_selftest.h]].

   RUNIT_ENABLE_VM_BENCH runs vm_bench_run() -- accessor resolve timings, see
   [[vm_bench.h]]. Safe to run alongside the self test: the test leaves the
   object table detached, and the benchmark's setup() rebuilds it before use.
   Worth running both after any change to the object layout or to the resolve
   path, so a timing win cannot quietly be a correctness loss.
   --------------------------------------------------------------------------- */
#define RUNIT_SKIP_DEVICE_INIT 1
#define RUNIT_ENABLE_VM_SELFTEST 0
#define RUNIT_ENABLE_VM_BENCH 0

/* Granular test section toggles (when RUNIT_ENABLE_VM_SELFTEST is 1) */
#define RUNIT_TEST_SECTION_OBJ 1     // Group 1: Object model, accessors, contracts (A-K, OBJ)
#define RUNIT_TEST_SECTION_LOADER 1  // Group 2: Loader & wire protocol (L, N, M, O, P)
#define RUNIT_TEST_SECTION_EXEC 1    // Group 3: Execution, blocks, math (R, BLK, S-Y, PIPE, PI, PRIME, OVERRIDE)
#define RUNIT_TEST_SECTION_SUB 1     // Group 4: Subscriptions & telemetry (SUB)

#define RUNIT_BOARD_POWER_LIMIT_MV 21000
#define RUNIT_BOARD_POWER_LIMIT_MA 5500
#define RUNIT_BOARD_POWER_BUDGET_MW (RUNIT_BOARD_POWER_LIMIT_MV * RUNIT_BOARD_POWER_LIMIT_MA / 1000)

static inline err_h sys_start_i2c(void) {
  i2c_master_bus_config_t bus0_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = SYS_PIN_I2C_0_SDA,
      .scl_io_num = SYS_PIN_I2C_0_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = SYS_I2C_PULLUP_ENABLE,
  };
  i2c_master_bus_config_t bus1_cfg = {
      .i2c_port = I2C_NUM_1,
      .sda_io_num = SYS_PIN_I2C_1_SDA,
      .scl_io_num = SYS_PIN_I2C_2_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = SYS_I2C_PULLUP_ENABLE,
  };
  return sys_i2c_init(&bus0_cfg, &bus1_cfg);
}

/*System-level power budget config (board mounted) - device creation lives in runit_board_devices.h*/
static inline err_h sys_power_static_config(void) {
  SE_ORIGIN_CALL(sys_power_set_limits(RUNIT_BOARD_POWER_LIMIT_MV, RUNIT_BOARD_POWER_LIMIT_MA, RUNIT_BOARD_POWER_BUDGET_MW));
  ESP_LOGI("static_config", "power limits configured");
  return NULL;
}

static inline err_h sys_ble_static_config(void) {
  SE_ORIGIN_CALL(sys_ble_init());

  sys_ble_svc_cfg_t runit_svc_cfg = {.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true};
  SE_ORIGIN_CALL(sys_ble_service_create(&runit_svc_cfg));

  sys_ble_char_create_t runit_chr_cfg_rx = {.info = {.uuid = SYS_BLE_CHR_RUNIT_RX, .is_write = true, .desc = "runit RX"}, .rx_buffer_size = 512, .rx_notify_sem = sys_interface_get_rx_wake_sem()};
  SE_ORIGIN_CALL(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_rx));

  sys_ble_char_create_t runit_chr_cfg_tx = {.info = {.uuid = SYS_BLE_CHR_RUNIT_TX, .is_notify = true, .desc = "runit TX"}, .rx_buffer_size = 0};
  SE_ORIGIN_CALL(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_tx));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_tx = {.header = PACKET_HEADER_TX, .size = 1024, .is_indication = false};
  SE_ORIGIN_CALL(sys_ble_char_assign_tx_buffer(SYS_BLE_CHR_RUNIT_TX, &runit_buff_cfg_tx));

  sys_ble_char_create_t runit_chr_cfg_status = {.info = {.uuid = SYS_BLE_CHT_RUNIT_STATUS, .is_notify = true, .desc = "runit Status"}, .rx_buffer_size = 0};
  SE_ORIGIN_CALL(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_status));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_status = {.header = PACKET_HEADER_STATUS, .size = 512, .is_indication = false};
  SE_ORIGIN_CALL(sys_ble_char_assign_tx_buffer(SYS_BLE_CHT_RUNIT_STATUS, &runit_buff_cfg_status));

  sys_ble_char_create_t runit_chr_cfg_logs = {.info = {.uuid = SYS_BLE_CHR_RUNIT_LOGS, .is_notify = true, .desc = "runit LOGS"}, .rx_buffer_size = 0};
  SE_ORIGIN_CALL(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &runit_chr_cfg_logs));

  sys_ble_tx_buf_cfg_t runit_buff_cfg_logs = {.header = PACKET_HEADER_LOGS, .size = 2048, .is_indication = false};
  SE_ORIGIN_CALL(sys_ble_char_assign_tx_buffer(SYS_BLE_CHR_RUNIT_LOGS, &runit_buff_cfg_logs));

  // Encoded error chains share the LOGS characteristic - the TX slot header is
  // what tells the two streams apart on the client side.
  sys_ble_tx_buf_cfg_t runit_buff_cfg_errors = {.header = PACKET_HEADER_ERRORS, .size = 1024, .is_indication = false};
  SE_ORIGIN_CALL(sys_ble_char_assign_tx_buffer(SYS_BLE_CHR_RUNIT_LOGS, &runit_buff_cfg_errors));

  SE_ORIGIN_CALL(sys_ble_database_sync());
  ESP_LOGI("static_config", "BLE initialized");
  return NULL;
}
