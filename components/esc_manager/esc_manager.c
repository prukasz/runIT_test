#include "esc_manager.h"

static const char * TAG = "ESC_MANAGER";

typedef struct{
    uint8_t id;
    uint16_t limit_min;
    uint16_t neutral;
    uint16_t limit_max;
    uint16_t range_us;
    uint16_t range_throttle;
}esc_instance_t;

static esc_instance_t* esc_list[16];

esp_err_t esc_manager_init(){
    if (pca9685.freq!=50){
       pca9685_set_pwm_frequency(&pca9685, 50);
    }
    return ESP_OK;
}

esp_err_t esc_manager_add(uint8_t gpio, uint8_t id){

    if (PWM_MNG_EMPTY != gpio_manager_check_pca9685(gpio)){
        return ESP_ERR_NOT_SUPPORTED;
    }
    gpio_manager_set_pca9685(gpio, PWM_MNG_ESC_SIMPLE);
    esc_list[gpio] = (esc_instance_t*)calloc(1, sizeof(esc_instance_t));
    esc_list[gpio] -> id = id;
    esc_list[gpio] -> limit_max = 200;
    esc_list[gpio] -> neutral = 100;
    esc_list[gpio] -> limit_min = 0;
    esc_list[gpio] -> range_us = 2000;
    esc_list[gpio] -> range_throttle = 200;
    return ESP_OK;
}

/*forward and brake*/
esp_err_t esc_manager_set_throttle(uint8_t gpio, uint16_t throttle){
     if (PWM_MNG_ESC_SIMPLE != gpio_manager_check_pca9685(gpio)){
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint8_t throttle_prepared;

    if (throttle > esc_list[gpio]->limit_max)
        throttle_prepared = esc_list[gpio]->limit_max;
    else if (throttle < esc_list[gpio]->limit_min)
    {
        throttle_prepared = esc_list[gpio]->limit_min;
    }
    else{
        throttle_prepared = throttle;
    }
    ESP_LOGI(TAG, "set throttle %d, %d", deg_to_duty (throttle_prepared, esc_list[gpio]->range_throttle, esc_list[gpio]->range_us), throttle_prepared);
    pca9685.channel_pwm_value[gpio] = deg_to_duty (throttle_prepared, esc_list[gpio]->range_throttle, esc_list[gpio]->range_us);
    return ESP_OK;
    }

esp_err_t esc_manager_set_neutral(uint8_t gpio){
     if (PWM_MNG_ESC_SIMPLE!= gpio_manager_check_pca9685(gpio)){
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "set neutral %d", deg_to_duty (esc_list[gpio]->neutral, esc_list[gpio]->range_throttle, esc_list[gpio]->range_us));
    pca9685.channel_pwm_value[gpio] = deg_to_duty (esc_list[gpio]->neutral, esc_list[gpio]->range_throttle, esc_list[gpio]->range_us);
    return ESP_OK;
}

esp_err_t esc_manager_arm(uint8_t gpio){
    ESP_LOGI(TAG, "arming sequence");
    pca9685.channel_pwm_value[gpio] = 205;
    #define ESC_ARM_DELAY pdMS_TO_TICKS(2000);
    pca9685.channel_pwm_value[gpio] = 410;
    #define ESC_ARM_DELAY pdMS_TO_TICKS(2000);
    esc_manager_set_neutral(gpio);
    return ESP_OK;
}