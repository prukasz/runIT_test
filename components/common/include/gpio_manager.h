#pragma once
#include "stdint.h"
#include "stdlib.h"

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

typedef enum{
    PWM_MNG_EMPTY = 0,
    PWM_MNG_SERVO,
    PWM_MNG_ESC_SIMPLE,
    PWM_MNG_ESC_BLDC,
    PWM_MNG_ESC_BLDC_HELI,
    PWM_MNG_HBRIDGE_A,
    PWM_MNG_HBRIDGE_B
}gpio_manager_pca_mode_t;

gpio_manager_pca_mode_t* gpio_manager_pca9685_init(void);
gpio_manager_mode_t* gpio_maganger_init(void);
