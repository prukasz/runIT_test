#pragma once
#include "stdint.h"

typedef enum{
    GPIO_MNG_EMPTY = 0,
    GPIO_MNG_STD_IO,
    GPIO_MNG_SDA,
    GPIO_MNG_SCL,
    GPIO_MNG_MOSI,
    GPIO_MNG_MISO,
    GPIO_MNG_CS,
    GPIO_MNG_AIN,
    GPIO_MNG_RMT,
    GPIO_MNG_OTHER
}gpio_manager_mode_t;

static gpio_manager_mode_t gpio_manager[30];


typedef enum{
    PWM_MNG_SERVO,
    PWM

}gpio_manager_mode_t;

static uint16_t gpio_pca9685_manager[16];
