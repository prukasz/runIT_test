#include "emulator_loop.h"
#include "emulator_errors.h"
#include "emulator_interface.h"
#include "emulator_body.h"
#include "emulator_parse.h"
#include "string.h"

/***********************************************************
In this file loop timer is created and managed
Simple loop watchdog implemented in timer callback
*************************************************************/

static const char *TAG = "EMULATOR_LOOP";

/*****************************************************************************/
/*      SEMAPHOPRES TO RUN LOOP AND WTD CHECK                                */

/**
*@brief This semaphore start loop
*/
SemaphoreHandle_t emu_global_loop_start_semaphore;
/**
*@brief This semaphore is used by emu watchdog to check if loop task finished in set
time, semaphore is given at the end of loop task
*/
SemaphoreHandle_t emu_global_loop_wtd_semaphore;

/*****************************************************************************/



/**
*@brief loop tick speed 
*/
static uint64_t loop_period_us = 100000;
static esp_timer_create_args_t loop_timer_params;
static esp_timer_handle_t loop_timer_handle;
static TaskHandle_t emulator_loop_body_handle;

static const uint32_t stack_depth = 10*1024;
static const UBaseType_t task_priority = 4;

/*********************************/
/**
 * @brief Emulator scope status
 *  for watchdog
 */
emu_status_t emu_global_status;
/*********************************/

static void IRAM_ATTR loop_tick_intr_handler(void *parameters) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (emu_global_loop_wtd_semaphore){
        if(pdTRUE ==xSemaphoreTakeFromISR(emu_global_loop_wtd_semaphore, &xHigherPriorityTaskWoken)){
            WTD_RESET();   //reset watchdog counter after taking back semaphore from loop
            LOOP_CNT_UP(); //mark that loop started
            xSemaphoreGiveFromISR(emu_global_loop_start_semaphore, &xHigherPriorityTaskWoken);
        }
        else{
            WTD_CNT_UP();  //count skipped timer interrupts
            if(WTD_EXCEEDED()){   //if skipped interrups exceed max amount halt loop(todo)
                WTD_SET();
                esp_timer_stop(loop_timer_handle);
                LOOP_SET_STATUS(LOOP_HALTED);
            }
        }
    }
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

emu_result_t emu_loop_init(void){
    emu_result_t res = {.code = EMU_OK};
    emu_global_loop_start_semaphore = xSemaphoreCreateBinary();
    emu_global_loop_wtd_semaphore   = xSemaphoreCreateBinary();
    WTD_SET_LIMIT(10);
    /*create task that will execute code*/
    xTaskCreate(emu_body_loop_task, TAG, stack_depth, NULL, task_priority, &emulator_loop_body_handle);
    ESP_LOGI(TAG,"Creating loop timer");
    loop_timer_params.name = TAG;
    loop_timer_params.callback = &loop_tick_intr_handler;
    loop_timer_params.dispatch_method = ESP_TIMER_ISR;
    ESP_ERROR_CHECK(esp_timer_create(&loop_timer_params, &loop_timer_handle));  
    LOOP_SET_STATUS(LOOP_SET);
    LOG_I(TAG, "Timer period set to %lld us", loop_period_us);
    xSemaphoreGive(emu_global_loop_wtd_semaphore);
    return res;
}


emu_result_t emu_loop_start(void) {
    // 1. Check if the system configuration allows running
    // We capture the result struct to check .code and propagate .flags if needed
    emu_result_t res = emu_parse_manager(PARSE_CHEKC_CAN_RUN);

    // If the manager says we can't run, return that specific error immediately.
    if (res.code != EMU_OK) {
        ESP_LOGE(TAG, "Cannot start loop: Verify failed with %s", EMU_ERR_TO_STR(res.code));
        return res; 
    }

    // 2. Check State Machine & Transition
    if (LOOP_STATUS_CMP(LOOP_SET)&&res.code==EMU_OK) {
        ESP_LOGI(TAG, "Starting loop (First Time)");
        LOOP_SET_STATUS(LOOP_RUNNING);

        esp_timer_start_periodic(loop_timer_handle, loop_period_us);
        return res;

    } 
    else if (LOOP_STATUS_CMP(LOOP_STOPPED)&&res.code==EMU_OK) {
        ESP_LOGI(TAG, "Starting loop (Resume form Stop)");
        LOOP_SET_STATUS(LOOP_RUNNING);
        esp_timer_start_periodic(loop_timer_handle, loop_period_us);
        return res;

    } 
    else if (LOOP_STATUS_CMP(LOOP_HALTED)&&res.code==EMU_OK) {
        ESP_LOGI(TAG, "Starting loop (Resume from Halt)");
        LOOP_SET_STATUS(LOOP_RUNNING);
        esp_timer_start_periodic(loop_timer_handle, loop_period_us);
        return res;

    } 
    else {
        ESP_LOGE(TAG, "Loop cannot be started: Invalid Status");
        res.code = EMU_ERR_INVALID_STATE;
        res.abort = true;
        return res;
    }
}

void emu_loop_period_set(uint64_t period_us){
    if(period_us > LOOP_PERIOD_MAX)
    {loop_period_us = LOOP_PERIOD_MAX;}
    if(period_us < LOOP_PERIOD_MIN)
    {loop_period_us = LOOP_PERIOD_MIN;}
    loop_period_us = period_us;
    return;
}


emu_result_t emu_loop_stop(void) {
    emu_result_t res = {.code = EMU_OK};
    if (!LOOP_STATUS_CMP(LOOP_RUNNING))
    {
        ESP_LOGE(TAG, "Loop is not running can't stop");
        res.code = EMU_ERR_INVALID_STATE;
        res.abort = true;
        return res;
    }
    ESP_LOGI(TAG, "Stopping loop");
    LOOP_SET_STATUS(LOOP_STOPPED);
    esp_timer_stop(loop_timer_handle);
    return res;
    }


