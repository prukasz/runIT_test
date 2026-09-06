#pragma once
/**
 * @file dec_sys_device_install.h
 * @brief Device installation packets for the "System Contracts" class (0x01).
 *
 * Split out from dec_sys_contracts.h: one packet per device type, headers
 * 0x40-0x47. Each payload is that device's own d_xxx_cfg_t flattened directly
 * onto the wire, so a device can be attached dynamically (at boot via a
 * sys_actions boot action, see SYS_ACTIONS.MD, or any time after) instead of
 * only through the hardcoded *_static_config() calls in runit.c.
 *
 * Included by dec_sys_contracts.h, which folds SYS_CONTRACTS_INSTALL_PACKET_LIST
 * into its own SYS_CONTRACTS_PACKET_LIST - same class byte (0x01), same
 * dec_sys_contracts_decode() dispatcher, just declared in a separate file so
 * dec_sys_contracts.h doesn't have to carry every device header's include.
 *
 * @note Every packet payload here is scalar fields only (uint8_t/uint16_t/
 * uint32_t/bool) - no nested struct types, even where the field is
 * conceptually "one thing" (a pin reference). A nested struct type is fine in
 * C, but it's one more special case any future codegen/tooling walking these
 * payloads would need to know about instead of just emitting a scalar per
 * field. A sys_io_pin_ref_t is therefore always three flat fields on the wire
 * - `<name>_device_id`, `<name>_pin`, `<name>_mode` - reassembled into the real
 * struct only on the C side, in the decoder.
 */

#include <stdint.h>
#include <sys/cdefs.h>
#include "devices.h"
#include "sys_error.h"
#include "sys_io.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_DEVICE_INSTALL

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_SYS_DEVICE_INSTALL_TAG "dec_sys_device_install"

/**
 * @brief Reassemble a sys_io_pin_ref_t from its three flat wire fields.
 *
 * @warning An unused pin MUST be encoded as pin == SYS_GPIO_NONE (0xFF) -
 *          device_id/mode are ignored in that case, exactly like
 *          SYS_IO_PIN_NONE on the C side (see sys_io.h). All-zero fields mean
 *          "device 0, pin 0" - a real pin, not "none".
 */
static inline sys_io_pin_ref_t pin_ref_from_wire(uint8_t device_id, uint8_t pin, uint8_t mode) {
  return (sys_io_pin_ref_t){.device_id = device_id, .pin = pin, .mode = (sys_io_mode_e)mode};
}

#define HEADER_packet_sys_device_install_gpio_esp_t 0x40
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_install_gpio_esp_t;

static inline err_h decoder_packet_sys_device_install_gpio_esp_t(packet_sys_device_install_gpio_esp_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing gpio_esp (dev %u)", packet->device_id);
  d_gpio_esp_cfg_t cfg = {.device_id = packet->device_id};
  return d_gpio_esp_create(&cfg);
}
#define HEADER_packet_sys_device_install_pca9685_t 0x41
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t oe_pin_device_id;
  uint8_t oe_pin_pin;
  uint8_t oe_pin_mode;
} packet_sys_device_install_pca9685_t;

static inline err_h decoder_packet_sys_device_install_pca9685_t(packet_sys_device_install_pca9685_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing pca9685 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_pca9685_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .oe_pin = pin_ref_from_wire(packet->oe_pin_device_id, packet->oe_pin_pin, packet->oe_pin_mode),
  };
  return d_pca9685_create(&cfg);
}

#define HEADER_packet_sys_device_install_tca6424a_t 0x42
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t intr_pin_device_id;
  uint8_t intr_pin_pin;
  uint8_t intr_pin_mode;
  uint8_t rst_pin_device_id;
  uint8_t rst_pin_pin;
  uint8_t rst_pin_mode;
} packet_sys_device_install_tca6424a_t;

static inline err_h decoder_packet_sys_device_install_tca6424a_t(packet_sys_device_install_tca6424a_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tca6424a (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tca6424a_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
      .rst_pin = pin_ref_from_wire(packet->rst_pin_device_id, packet->rst_pin_pin, packet->rst_pin_mode),
  };
  return d_tca6424a_create(&cfg);
}

#define HEADER_packet_sys_device_install_tps55289_t 0x43
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t intr_pin_device_id;
  uint8_t intr_pin_pin;
  uint8_t intr_pin_mode;
  uint8_t en_pin_device_id;
  uint8_t en_pin_pin;
  uint8_t en_pin_mode;
} packet_sys_device_install_tps55289_t;

static inline err_h decoder_packet_sys_device_install_tps55289_t(packet_sys_device_install_tps55289_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tps55289 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tps55289_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
      .en_pin = pin_ref_from_wire(packet->en_pin_device_id, packet->en_pin_pin, packet->en_pin_mode),
  };
  return d_tps55289_create(&cfg);
}

#define HEADER_packet_sys_device_install_ina3221_t 0x44
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t crit_pin_device_id;
  uint8_t crit_pin_pin;
  uint8_t crit_pin_mode;
  uint8_t warn_pin_device_id;
  uint8_t warn_pin_pin;
  uint8_t warn_pin_mode;
} packet_sys_device_install_ina3221_t;

static inline err_h decoder_packet_sys_device_install_ina3221_t(packet_sys_device_install_ina3221_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ina3221 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ina3221_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .crit_pin = pin_ref_from_wire(packet->crit_pin_device_id, packet->crit_pin_pin, packet->crit_pin_mode),
      .warn_pin = pin_ref_from_wire(packet->warn_pin_device_id, packet->warn_pin_pin, packet->warn_pin_mode),
  };
  return d_ina3221_create(&cfg);
}

#define HEADER_packet_sys_device_install_ap33772s_t 0x45
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t intr_pin_device_id;
  uint8_t intr_pin_pin;
  uint8_t intr_pin_mode;
} packet_sys_device_install_ap33772s_t;

static inline err_h decoder_packet_sys_device_install_ap33772s_t(packet_sys_device_install_ap33772s_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ap33772s (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ap33772s_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
  };
  return d_ap33772s_create(&cfg);
}

#define HEADER_packet_sys_device_install_dac53202_t 0x46
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
} packet_sys_device_install_dac53202_t;

static inline err_h decoder_packet_sys_device_install_dac53202_t(packet_sys_device_install_dac53202_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing dac53202 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_dac53202_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
  };
  return d_dac53202_create(&cfg);
}

#define HEADER_packet_sys_device_install_ads7128_t 0x47
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t intr_pin_device_id;
  uint8_t intr_pin_pin;
  uint8_t intr_pin_mode;
  uint32_t vref_mv;
} packet_sys_device_install_ads7128_t;

static inline err_h decoder_packet_sys_device_install_ads7128_t(packet_sys_device_install_ads7128_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ads7128 (dev %u, i2c bus %u addr 0x%02X, vref %lu mV)", packet->device_id, packet->i2c_bus, packet->i2c_addr, (unsigned long)packet->vref_mv);
  d_ads7128_cfg_t cfg = {
      .device_id = packet->device_id,
      .i2c_bus = packet->i2c_bus != 0,
      .i2c_addr = packet->i2c_addr,
      .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
      .vref_mv = packet->vref_mv,
  };
  return d_ads7128_create(&cfg);
}

#define SYS_CONTRACTS_INSTALL_PACKET_LIST(X)                                                                                         \
  X(HEADER_packet_sys_device_install_gpio_esp_t, packet_sys_device_install_gpio_esp_t, decoder_packet_sys_device_install_gpio_esp_t) \
  X(HEADER_packet_sys_device_install_pca9685_t, packet_sys_device_install_pca9685_t, decoder_packet_sys_device_install_pca9685_t)    \
  X(HEADER_packet_sys_device_install_tca6424a_t, packet_sys_device_install_tca6424a_t, decoder_packet_sys_device_install_tca6424a_t) \
  X(HEADER_packet_sys_device_install_tps55289_t, packet_sys_device_install_tps55289_t, decoder_packet_sys_device_install_tps55289_t) \
  X(HEADER_packet_sys_device_install_ina3221_t, packet_sys_device_install_ina3221_t, decoder_packet_sys_device_install_ina3221_t)    \
  X(HEADER_packet_sys_device_install_ap33772s_t, packet_sys_device_install_ap33772s_t, decoder_packet_sys_device_install_ap33772s_t) \
  X(HEADER_packet_sys_device_install_dac53202_t, packet_sys_device_install_dac53202_t, decoder_packet_sys_device_install_dac53202_t) \
  X(HEADER_packet_sys_device_install_ads7128_t, packet_sys_device_install_ads7128_t, decoder_packet_sys_device_install_ads7128_t)
