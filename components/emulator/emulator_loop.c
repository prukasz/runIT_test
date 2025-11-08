#include "emulator_loop.h"

SemaphoreHandle_t loop_semaphore;
SemaphoreHandle_t wtd_semaphore;
extern void loop_task(void* params);
static const uint32_t stack_depth = 10*1024;
static const UBaseType_t task_priority = 4;
emu_wtd_t wtd;

static const char *TAG = "LOOP_TIMER";
static uint64_t loop_period_us = 100000;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static TaskHandle_t emulator_body_handle;
static loop_status_t loop_status = LOOP_NOT_SET;

static void IRAM_ATTR loop_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (wtd_semaphore){
        if(pdTRUE ==xSemaphoreTakeFromISR(wtd_semaphore, &xHigherPriorityTaskWoken)){
            wtd.wtd_triggered = false;
            wtd.loops_skipped = 0;
            xSemaphoreGiveFromISR(loop_semaphore, &xHigherPriorityTaskWoken);
            wtd.loop_counter++;
        }
        else{
            wtd.wtd_triggered = true;
            wtd.loops_skipped++;
            wtd.loop_counter++;
            if(wtd.loops_skipped >= wtd.max_skipp){
                esp_timer_stop(loop_timer_handle);
                loop_status = LOOP_HALTED;
            }
        }

    }
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

emu_err_t loop_init(){
    loop_semaphore = xSemaphoreCreateBinary();
    wtd_semaphore = xSemaphoreCreateBinary();
    wtd.max_skipp = 3;
    xTaskCreate(loop_task, TAG, stack_depth, NULL, task_priority, &emulator_body_handle);
    ESP_LOGI(TAG, "Creating loop timer");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));  
    loop_status = LOOP_SET;
    xSemaphoreGive(wtd_semaphore);
    return EMU_OK;
}


emu_err_t loop_start(void) {
    if(loop_status == LOOP_SET){
        loop_status = LOOP_RUNNING;
        ESP_LOGI(TAG, "Starting loop first time");
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else if (loop_status == LOOP_STOPPED){
        loop_status = LOOP_RUNNING;
        ESP_LOGI(TAG, "Starting loop after stop");
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else if (loop_status == LOOP_HALTED){
        ESP_LOGI(TAG, "Starting loop after halt");
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else{
        ESP_LOGE(TAG, "Loop cannot be started");
        return EMU_ERR_INVALID_STATE;
    }
    return EMU_ERR_UNLIKELY;
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
