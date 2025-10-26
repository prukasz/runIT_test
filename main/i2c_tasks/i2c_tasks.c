#include "i2c_tasks.h"

extern uint8_t chr_val_1[23];

QueueHandle_t mpu6050_queue;

static void IRAM_ATTR mpu6050_intr_handler(void *parameters)
{
    mpu6050_handle_t *handle = (mpu6050_handle_t*)parameters;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(mpu6050_queue, &handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

void task_pca9685(void *parameters){
    pca9685_handle_t *handle = (pca9685_handle_t *)parameters;
    pca9685_restart(handle);
    pca9685_set_pwm_frequency(handle, 50);
    while (1)
    {
        pca9685_update_pwm_values(handle, 0);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP_LOGI("pca", "pca set");
    }
};

void task_ads1115 ( void *parameters){
    ads1115_handle_t *handle = (ads1115_handle_t *)parameters;
    ads1115_set_mux(handle, ADS1115_MUX_0_GND);
    ads1115_config_set_sps(handle, ADS1115_SPS_860);
    TickType_t uxprev_time;
    while (1)
    {
        ads1115_read_raw(handle);
        ESP_LOGI("ADS", "%0x", handle->last_reading);
        chr_val_1[0] = (uint8_t)((handle->last_reading & 0xFF00)>>8);
        chr_val_1[1] = (uint8_t)(handle->last_reading & 0x00FF);
        vTaskDelayUntil(&uxprev_time, 1000);
    }
};

void task_mpu6050(void *parameters){
    mpu6050_handle_t *handle = (mpu6050_handle_t*)parameters;
    handle->intr_config = (mpu6050_intr_config_t){
        .active_level = MPU6050_INTR_PIN_ACTIVE_L,
        .interrupt_clear_mode = MPU6050_INTR_CLEAR_ON_STATUS,
        .interrupt_latch = MPU6050_INTR_LATCH_UNTIL_CLEARED,
        .interrupt_pin = GPIO_NUM_13,
        .pin_mode = MPU6050_INTR_OD,
        .intr_source_bitmask = MPU6050_INTR_MOT_DETECT,
        .treshold_cfg.mot_dur = 0x01,
        .treshold_cfg.mot_thr = 0x01,
        .treshold_cnt.mot_cnt = 0x01
    };
    mpu6050_queue = xQueueCreate(1, sizeof(int));
    ESP_ERROR_CHECK(mpu6050_config(handle, MPU6050_ACCE_FS_4G, MPU6050_GYRO_FS_500DPS));
    ESP_ERROR_CHECK(mpu6050_wake_up(handle));
    ESP_ERROR_CHECK(mpu6050_intr_config(handle));
    ESP_ERROR_CHECK(mpu6050_intr_src_enable(handle));
    ESP_ERROR_CHECK(mpu6050_intr_isr_add(handle, mpu6050_intr_handler));
    mpu6050_handle_t *handle2;
    while(1){
        if(xQueueReceive(mpu6050_queue, &handle2, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI("MPU6050", "Motion detected pin: %d", handle2->intr_config.interrupt_pin);
            ESP_ERROR_CHECK(mpu6050_get_gyro(handle));
            ESP_ERROR_CHECK(mpu6050_get_acce(handle));
            ESP_LOGI("MPU6505", "gyro -> %f, %f ,%f",
                    handle->gyro_value.x,  handle->gyro_value.y,  handle->gyro_value.z);
            ESP_LOGI("MPU6505", "accel -> %f, %f ,%f",
                    handle->acce_value.x,  handle->acce_value.y,  handle->acce_value.z);
            vTaskDelay(pdMS_TO_TICKS(1000));
            uint8_t status;
            ESP_ERROR_CHECK(mpu6050_intr_get_status(handle, &status));
        }
    }
}