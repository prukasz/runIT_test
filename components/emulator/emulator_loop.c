#include "emulator_loop.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern SemaphoreHandle_t emulator_start;

static const char *TAG = "LOOP_TIMER";
static uint64_t loop_period_us;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static loop_status_t loop_status = EMU_NOT_SET;

static void IRAM_ATTR loop_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(emulator_start){
        xSemaphoreGiveFromISR(emulator_start, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

emulator_err_t loop_create_set_period(uint64_t period) {
    ESP_LOGI(TAG, "Creating loop timer");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));

    if (period > LOOP_PERIOD_MAX) period = LOOP_PERIOD_MAX;
    if (period < LOOP_PERIOD_MIN) period = LOOP_PERIOD_MIN;

    loop_period_us = period;
    loop_status = EMU_SET;
    return EMU_OK;
}

emulator_err_t loop_start(void) {
    ESP_LOGI(TAG, "Starting loop");
    ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
    loop_status = EMU_RUNNING;
    return EMU_OK;
}

emulator_err_t loop_stop(void) {
    ESP_LOGI(TAG, "Stopping loop");
    esp_timer_stop(loop_timer_handle);
    loop_status = EMU_STOPPED;
    return EMU_OK;
}
