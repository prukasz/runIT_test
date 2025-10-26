#include "servo_manager.h"
static const char *TAG = "SERVO MANAGER";
#define MAXGPIO 16


static inline uint16_t deg_to_duty(uint16_t angle_deg, uint16_t range_deg, uint16_t range_us) {
    uint16_t min_us = 1500 - (range_us / 2);
    uint16_t pulse_us = min_us + ((uint32_t)angle_deg * range_us) / range_deg;
    return (uint16_t)((pulse_us * 4095) / 20000);
}

typedef struct{
    uint8_t id;
    uint8_t gpio;
    bool is_360;
    uint16_t range_us;
    uint16_t range_dergrees;
    uint16_t neutral_pos;
    uint16_t limit_min;
    uint16_t limit_max;
}servo_instance_t;

static servo_instance_t *servo_list[16];

esp_err_t servo_manager_init(){
    if (pca9685.freq!=50){
       pca9685_set_pwm_frequency(&pca9685, 50);
    }
    return ESP_OK;
}

esp_err_t servo_manager_add(uint8_t gpio, uint8_t id)
{
    if (PWM_MNG_EMPTY != gpio_manager_check_pca9685(gpio))
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    gpio_manager_set_pca9685(gpio, PWM_MNG_SERVO);
    servo_list[gpio] = (servo_instance_t*)calloc(1, sizeof(servo_instance_t));
    servo_list[gpio]->id = id;
    servo_list[gpio]->neutral_pos = 307;
    servo_list[gpio]->limit_min = 205;
    servo_list[gpio]->limit_max = 410;
    servo_list[gpio]->range_us = 1000;
    servo_list[gpio]->range_dergrees = 180;
    servo_list[gpio]->is_360 = 0;
    return ESP_OK;
}

esp_err_t servo_manager_configure(uint8_t gpio, uint16_t range_us, uint16_t range_deg, uint8_t neutral_pos, uint8_t max_angle,
    uint8_t min_angle, bool is360){
    if (gpio_manager_check_pca9685(gpio) != PWM_MNG_SERVO){
        ESP_LOGW(TAG, "Invalid servo selected");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (is360){
        ESP_LOGI(TAG, "selected 360 servo neutral at 1500us /n ");
        servo_list[gpio]->range_us = range_us;
        servo_list[gpio]->neutral_pos = neutral_pos;
        servo_list[gpio]->limit_min = min_angle;//deg_to_duty(min_angle, range_deg, range_us);
        servo_list[gpio]->limit_max = max_angle;//deg_to_duty(max_angle, range_deg, range_us);
        servo_list[gpio]->range_us = range_us;
        servo_list[gpio]->range_dergrees = range_deg;
        servo_list[gpio]->is_360 = 1;
    }else{
        ESP_LOGI(TAG, "selected normal servo");
        servo_list[gpio]->range_us = range_us;
        servo_list[gpio]->neutral_pos = neutral_pos;
        servo_list[gpio]->limit_min = min_angle;//deg_to_duty(min_angle, range_deg, range_us);
        servo_list[gpio]->limit_max = max_angle;//deg_to_duty(max_angle, range_deg, range_us);
        servo_list[gpio]->range_us = range_us;
        servo_list[gpio]->range_dergrees = range_deg;
        servo_list[gpio]->is_360 = 0;
    }
    return ESP_OK;
}
esp_err_t servo_manager_delete(uint8_t gpio){
    if(PWM_MNG_SERVO == gpio_manager_check_pca9685(gpio)){
    free(servo_list[gpio]);
    gpio_manager_set_pca9685(gpio, PWM_MNG_EMPTY);
    return ESP_OK;

    }
    else if(PWM_MNG_EMPTY == gpio_manager_check_pca9685(gpio))
    {
        return ESP_OK;
    }
    return ESP_OK;
}
esp_err_t servo_manager_set_angle(uint8_t gpio, uint8_t angle){
    if(PWM_MNG_SERVO!=gpio_manager_check_pca9685(gpio)){
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t angle_prepared;

    if (angle > servo_list[gpio]->limit_max)
        angle_prepared = servo_list[gpio]->limit_max;
    else if (angle < servo_list[gpio]->limit_min)
    {
        angle_prepared = servo_list[gpio]->limit_min;
    }
    else{
        angle_prepared = angle;
    }
    pca9685.channel_pwm_value[gpio] = deg_to_duty (angle_prepared, servo_list[gpio]->range_dergrees, servo_list[gpio]->range_us);
    ESP_LOGI(TAG, "servo duty %d", pca9685.channel_pwm_value[gpio]);
    return ESP_OK;
}

esp_err_t servo_manager_neutral(uint8_t gpio){
    if(PWM_MNG_SERVO!=gpio_manager_check_pca9685(gpio)){
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint8_t angle_prepared;
    if (servo_list[gpio]->neutral_pos > servo_list[gpio]->limit_max)
        angle_prepared = servo_list[gpio]->limit_max;
    else if (servo_list[gpio]->neutral_pos < servo_list[gpio]->limit_min)
    {
        angle_prepared = servo_list[gpio]->limit_min;
    }
    else{
        angle_prepared = servo_list[gpio]->neutral_pos;
    }
    pca9685.channel_pwm_value[gpio] = deg_to_duty (angle_prepared, servo_list[gpio]->range_dergrees, servo_list[gpio]->range_us);
    ESP_LOGI(TAG, "servo duty %d", pca9685.channel_pwm_value[gpio]);
    return ESP_OK;
}
