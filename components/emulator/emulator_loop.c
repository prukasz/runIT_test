#include "emulator_loop.h"
#include "emulator.h"
#include "emulator_body.h"
#include "emulator_parse.h"

static const char *TAG = "LOOP_TIMER";

SemaphoreHandle_t loop_semaphore;
SemaphoreHandle_t wtd_semaphore;

static const uint32_t stack_depth = 10*1024;
static const UBaseType_t task_priority = 4;
static uint64_t loop_period_us = 100000;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static TaskHandle_t emulator_body_handle;
static loop_status_t loop_status = LOOP_NOT_SET;

emu_status_t status;
static void IRAM_ATTR loop_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (wtd_semaphore){
        if(pdTRUE ==xSemaphoreTakeFromISR(wtd_semaphore, &xHigherPriorityTaskWoken)){
            WTD_RESET();
            LOOP_CNT_UP();
            xSemaphoreGiveFromISR(loop_semaphore, &xHigherPriorityTaskWoken);
        }
        else{
            WTD_CNT_UP();
            if(WTD_EXCEEDED()){
                WTD_SET();
                esp_timer_stop(loop_timer_handle);
                LOOP_SET_STATUS(LOOP_HALTED);
            }
        }
    }
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

emu_err_t loop_init(){
    
    loop_semaphore = xSemaphoreCreateBinary();
    wtd_semaphore = xSemaphoreCreateBinary();
    WTD_SET_LIMIT(2);
    xTaskCreate(loop_task, TAG, stack_depth, NULL, task_priority, &emulator_body_handle);
    ESP_LOGI(TAG, "Creating loop timer");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));  
    LOOP_SET_STATUS(LOOP_SET);
    xSemaphoreGive(wtd_semaphore);
    return EMU_OK;
}


emu_err_t loop_start(void) {
    if(LOOP_STATUS_CMP(LOOP_SET)&&PARSE_FINISHED()){
        LOOP_SET_STATUS(LOOP_RUNNING);
        ESP_LOGI(TAG, "Starting loop first time");
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else if (LOOP_STATUS_CMP(LOOP_STOPPED)){
        LOOP_SET_STATUS(LOOP_RUNNING);
        ESP_LOGI(TAG, "Starting loop after stop");
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else if (LOOP_STATUS_CMP(LOOP_HALTED)){
        ESP_LOGI(TAG, "Starting loop after halt");
        LOOP_SET_STATUS(LOOP_RUNNING);
        ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
        return EMU_OK;
    }else{
        ESP_LOGE(TAG, "Loop cannot be started");
        return EMU_ERR_INVALID_STATE;
    }
    return EMU_ERR_UNLIKELY;
}

void loop_set_period(uint64_t period_us){
    if(period_us > LOOP_PERIOD_MAX)
    {loop_period_us = LOOP_PERIOD_MAX;}
    if(period_us < LOOP_PERIOD_MIN)
    {loop_period_us = LOOP_PERIOD_MIN;}
    loop_period_us = period_us;
    return;
}


emu_err_t loop_stop(void) {
    if (!LOOP_STATUS_CMP(LOOP_RUNNING))
    {
        ESP_LOGE(TAG, "Loop is not running can't stop");
        return EMU_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Stopping loop");
    esp_timer_stop(loop_timer_handle);
    LOOP_SET_STATUS(LOOP_STOPPED);
    return EMU_OK;
    }
