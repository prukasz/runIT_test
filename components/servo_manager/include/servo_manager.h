#pragma once

#include "stdint.h"
#include "esp_err.h"
#include "stdbool.h"
#include "pca9685.h"
#include "gpio_manager.h"
#include "string.h"
#include "esp_log.h"

/* this module does not provide direct i2c servo steering
 only prepares data and manager servos for pca9685*/


esp_err_t servo_manager_init();
esp_err_t servo_manager_add(uint8_t gpio, uint8_t id);
esp_err_t servo_manager_configure(uint8_t gpio, uint16_t range_us, uint16_t range_deg, uint8_t neutral_pos, uint8_t max_angle,
    uint8_t min_angle, bool is360);
esp_err_t servo_manager_delete(uint8_t gpio);
esp_err_t servo_manager_set_angle(uint8_t gpio, uint8_t angle);
esp_err_t servo_manager_neutral(uint8_t gpio);

extern pca9685_handle_t pca9685;


