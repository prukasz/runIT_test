#include "emulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "LOOP_TIMER";
static uint64_t loop_period_us;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static loop_status_t loop_status = LOOP_NOT_SET;

static void IRAM_ATTR loop_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // xQueueSendFromISR(...);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

loop_err_t loop_create_set_period(uint64_t period) {
    ESP_LOGI(TAG, "Creating loop timer...");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));

    if (period > LOOP_PERIOD_MAX) period = LOOP_PERIOD_MAX;
    if (period < LOOP_PERIOD_MIN) period = LOOP_PERIOD_MIN;

    loop_period_us = period;
    loop_status = LOOP_SET;
    return LOOP_ERR_OK;
}

loop_err_t loop_start(void) {
    ESP_LOGI(TAG, "Starting loop");
    ESP_ERROR_CHECK(esp_timer_start_periodic(loop_timer_handle, loop_period_us));
    loop_status = LOOP_RUNNING;
    return LOOP_ERR_OK;
}

loop_err_t loop_stop(void) {
    ESP_LOGI(TAG, "Stopping loop");
    esp_timer_stop(loop_timer_handle);
    loop_status = LOOP_STOPPED;
    return LOOP_ERR_OK;
}
