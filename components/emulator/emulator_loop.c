#include "emulator_loop.h"

extern SemaphoreHandle_t loop_semaphore;
static const uint32_t stack_depth = 10*1024;
static const UBaseType_t task_priority = 4;

static const char *TAG = "LOOP_TIMER";
static uint64_t loop_period_us = 10000;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static TaskHandle_t emulator_body_handle;
static loop_status_t loop_status = LOOP_NOT_SET;

int timer_counter = 0;
static void IRAM_ATTR loop_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if(loop_semaphore){
        timer_counter++;
        xSemaphoreGiveFromISR(loop_semaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();

}

emu_err_t loop_init(){
    loop_semaphore = xSemaphoreCreateBinary();
    xTaskCreate(loop_task, TAG, stack_depth, NULL, task_priority, &emulator_body_handle);
        ESP_LOGI(TAG, "Creating loop timer");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));  
    loop_status = LOOP_SET;
    return EMU_OK;
}


emu_err_t loop_start(void) {
    if (loop_status != LOOP_SET && loop_status != LOOP_STOPPED)
    {
        ESP_LOGE(TAG, "can't run emu not initialized");
        return EMU_ERR_INVALID_STATE;
    }
    else if (loop_status == LOOP_RUNNING)
    {
        ESP_LOGW(TAG, "loop already running");
        return EMU_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting loop");
    ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
    loop_status = LOOP_RUNNING;
    return EMU_OK;
}



emu_err_t loop_stop(void) {
    if (loop_status != LOOP_RUNNING)
    {
        ESP_LOGE(TAG, "Loop is not running can't stop");
        return EMU_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Stopping loop");
    esp_timer_stop(loop_timer_handle);
    loop_status = LOOP_STOPPED;
    return EMU_OK;
    }
