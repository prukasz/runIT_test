#pragma once
/******************************************************
Board onboard device creation
!Board rev. 1.0

Bound to sys_actions' boot action (id 0) - see runit.c. Device id / addresses
shall not be changed; devices below are commented out where the chip isn't
populated on this board rev.
****************************************************** */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "devices.h"
#include "runit_board_cfg.h"
#include "runit_board_defs.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Boot action (static action id 0): creates onboard devices at boot.
 *
 * Matches action_static_func_t, bound to sys_actions id 0 via
 * sys_actions_bind_static() before sys_actions_init() runs.
 */
err_h runit_at_boot(void* arg) {
  (void)arg;
  SE_ORIGIN_CALL(d_gpio_esp_create(&(d_gpio_esp_cfg_t){
      .device_id = DEVICE_ID_GPIO_ESP,
  }));
  SE_ORIGIN_CALL(d_tca6424a_create(&(d_tca6424a_cfg_t){
      .device_id = DEVICE_ID_TCA6424A,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x23,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT),
      .rst_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 8, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_ads7128_create(&(d_ads7128_cfg_t){
      .device_id = DEVICE_ID_ADS7128,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x10,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_GPIO_ESP, 42, SYS_IO_MODE_INPUT_PULLUP),
      .vref_mv = 20000,
  }));
  SE_ORIGIN_CALL(d_pca9685_create(&(d_pca9685_cfg_t){
      .device_id = DEVICE_ID_PCA9685, .i2c_bus = SYS_I2C_BUS_INTERNAL, .i2c_addr = 0x60, .oe_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 0, SYS_IO_MODE_OUTPUT_PUSH_PULL)  // rev 1.0: OE not driven by the expander
  }));
  // SE_ORIGIN_CALL(d_dac53202_create(&(d_dac53202_cfg_t){
  //     .device_id = DEVICE_ID_DAC53202,
  //     .i2c_bus = SYS_I2C_BUS_INTERNAL,
  //     .i2c_addr = 0x13,
  // }));
  SE_ORIGIN_CALL(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_0,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x74,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 1, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 17, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_tps55289_create(&(d_tps55289_cfg_t){
      .device_id = DEVICE_ID_TPS55289_1,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x75,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 2, SYS_IO_MODE_INPUT),
      .en_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 16, SYS_IO_MODE_OUTPUT_PUSH_PULL),
  }));
  SE_ORIGIN_CALL(d_ina3221_create(&(d_ina3221_cfg_t){
      .device_id = DEVICE_ID_INA3221,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x40,
      .crit_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 5, SYS_IO_MODE_INPUT),
      .warn_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 6, SYS_IO_MODE_INPUT),
  }));
  SE_ORIGIN_CALL(d_ap33772s_create(&(d_ap33772s_cfg_t){
      .device_id = DEVICE_ID_AP33772S,
      .i2c_bus = SYS_I2C_BUS_INTERNAL,
      .i2c_addr = 0x52,
      .intr_pin = SYS_IO_PIN_INIT(DEVICE_ID_TCA6424A, 12, SYS_IO_MODE_INPUT),
  }));
  // leds
  SE_ORIGIN_CALL(sys_io_set_mode(1, 22, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  SE_ORIGIN_CALL(sys_io_set_mode(1, 23, SYS_IO_MODE_OUTPUT_PUSH_PULL));
  ESP_LOGI("board_devices", "onboard devices created");
  return NULL;
}
