#pragma once 
#include "esp_err.h"
#include "esp_log.h"
#include "pca9685.h"
#include "servo_manager.h"
#include "gpio_manager.h"
#include "freertos/task.h"

extern pca9685_handle_t pca9685;
#define ESC_ARM_DELAY pdMS_TO_TICKS(2000);

esp_err_t esc_manager_init();
esp_err_t esc_manager_add(uint8_t gpio, uint8_t id);
esp_err_t esc_manager_set_throttle(uint8_t gpio, uint16_t throttle);
esp_err_t esc_manager_set_neutral(uint8_t gpio);
esp_err_t esc_manager_arm(uint8_t gpio);
