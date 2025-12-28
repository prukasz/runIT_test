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
/**
*@brief loop tick speed 
*/
static uint64_t loop_period_us = 100000;
static const uint32_t stack_depth = 10*1024;

static void IRAM_ATTR loop_tick_intr_handler(void *arg) {
    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    //calculate running time in ms (time stretchng possible)
    ctx->timer.time+=(ctx->timer.loop_period/1000);
    if (xSemaphoreTakeFromISR(ctx->sem_loop_wtd, &xHigherPriorityTaskWoken) == pdTRUE) {
        ctx->wtd.loops_skipped = 0;
        ctx->wtd.wtd_triggered = 0;
        ctx->timer.loop_counter++;
        xSemaphoreGiveFromISR(ctx->sem_loop_start, &xHigherPriorityTaskWoken);
    }
    else {
        if (ctx->wtd.wtd_active) {
            ctx->wtd.loops_skipped++;
            
            if (ctx->wtd.loops_skipped > ctx->wtd.max_skipp) {
                ctx->wtd.wtd_triggered = 1;
                esp_timer_stop(ctx->timer.timer_handle);
                ctx->timer.loop_status = LOOP_HALTED;
            }
        }
    }
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}


emu_loop_handle_t emu_loop_init(uint64_t period_us){
    emu_loop_ctx_t *ctx = calloc(1, sizeof(emu_loop_ctx_t));
    if(!ctx){
        ESP_LOGE(TAG, "Failed to allocate memory for loop context");
        return NULL;
    }
    ctx->wtd.max_skipp = 5;
    ctx->wtd.wtd_active = 1;
    ctx->wtd.wtd_triggered = 0;
    ctx->timer.loop_period = period_us;
    ctx->timer.loop_status = LOOP_CREATED;
    ctx->sem_loop_start = xSemaphoreCreateBinary();
    ctx->sem_loop_wtd = xSemaphoreCreateBinary();
    if(!ctx->sem_loop_start || !ctx->sem_loop_wtd) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        free(ctx);
        return NULL;
    }
    xTaskCreate(emu_body_loop_task, "EMU_LOOP", stack_depth, (void*)ctx, 4, NULL);
    const esp_timer_create_args_t timer_args = {
        .callback = &loop_tick_intr_handler,
        .arg = (void*)ctx,
        .name = "EMU_TIMER",
        .dispatch_method = ESP_TIMER_ISR
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->timer.timer_handle));
    xSemaphoreGive(ctx->sem_loop_wtd);
    return (emu_loop_handle_t)ctx;
}


emu_result_t emu_loop_start(emu_loop_handle_t handle){
    if (!handle) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)handle;
    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {
        ESP_LOGE(TAG, "Cannot start loop: Validation failed: %s", EMU_ERR_TO_STR(res.code));
        return res;
    }
    bool should_start_timer = false;
    if (ctx->timer.loop_status == LOOP_CREATED) {
        ESP_LOGI(TAG, "Starting loop (First Time)");
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (ctx->timer.loop_status == LOOP_STOPPED) {
        ESP_LOGI(TAG, "Resuming loop (From Stopped)");
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (ctx->timer.loop_status == LOOP_HALTED) {
        ESP_LOGI(TAG, "Resuming loop (From Halted)");
        /* Reset WTD flags on resume */
        ctx->wtd.wtd_triggered = 0;
        ctx->wtd.loops_skipped = 0;
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else {
        ESP_LOGW(TAG, "Loop start requested but state is %d (Already Running?)", ctx->timer.loop_status);
        return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_STATE, 0xFFFF);
    }
    if (should_start_timer) {
        xSemaphoreGive(ctx->sem_loop_start);
        /* Start the hardware timer */
        esp_timer_start_periodic(ctx->timer.timer_handle, ctx->timer.loop_period);
    }
    return EMU_RESULT_OK();
}

emu_result_t emu_loop_stop(emu_loop_handle_t handle) {
    if (!handle) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)handle;

    if (ctx->timer.loop_status != LOOP_RUNNING) {
        ESP_LOGW(TAG, "Attempted to stop loop, but state is %d (Not Running)", ctx->timer.loop_status);
        return EMU_RESULT_WARN(EMU_ERR_INVALID_STATE, 0xFFFF);
    }

    ESP_LOGI(TAG, "Stopping loop");
    ctx->timer.loop_status = LOOP_STOPPED;

    if (esp_timer_stop(ctx->timer.timer_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop hardware timer");
        ctx->timer.loop_status = LOOP_HALTED;
        return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_STATE, 0xFFFF);
    }
    return EMU_RESULT_OK();
}

void emu_loop_set_period(emu_loop_handle_t handle, uint64_t period_us) {
    if (!handle) {
        ESP_LOGE(TAG, "set_period called with NULL handle");
        return;
    }

    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)handle;

    //clamp to limits
    if (period_us > LOOP_PERIOD_MAX) {
        ESP_LOGW(TAG, "Requested period %llu us too slow, clamping to %d us", period_us, LOOP_PERIOD_MAX);
        period_us = LOOP_PERIOD_MAX;
    } 
    else if (period_us < LOOP_PERIOD_MIN) {
        ESP_LOGW(TAG, "Requested period %llu us too fast, clamping to %d us", period_us, LOOP_PERIOD_MIN);
        period_us = LOOP_PERIOD_MIN;
    }

    uint64_t old_period = ctx->timer.loop_period;
    ctx->timer.loop_period = period_us;

    /*Apply Immediately if Running */
    if (ctx->timer.loop_status == LOOP_RUNNING) {
        esp_timer_stop(ctx->timer.timer_handle);
        esp_timer_start_periodic(ctx->timer.timer_handle, ctx->timer.loop_period);
        ESP_LOGI(TAG, "Loop period updated live: %llu -> %llu us", old_period, ctx->timer.loop_period);
    } else {
        ESP_LOGI(TAG, "Loop period config updated: %llu -> %llu us (Will apply on next start)", old_period, ctx->timer.loop_period);
    }
}

emu_result_t emu_loop_run_once(emu_loop_handle_t handle) {
    if (!handle) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }

    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)handle;

    if (ctx->timer.loop_status == LOOP_RUNNING) {
        ESP_LOGW(TAG, "Cannot run_once while loop is RUNNING. Stop it first.");
        return EMU_RESULT_WARN(EMU_ERR_INVALID_STATE, 0xFFFF);
    }

    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {
        return res; 
    }

    if (xSemaphoreTake(ctx->sem_loop_wtd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Run Once failed: System is busy or WTD semaphore unavailable");
        return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_STATE, 0xFFFF);
    }
    xSemaphoreGive(ctx->sem_loop_start);
    //We manually check wtd
    if (xSemaphoreTake(ctx->sem_loop_wtd, pdMS_TO_TICKS(ctx->wtd.max_skipp *loop_period_us)) == pdTRUE) {

        xSemaphoreGive(ctx->sem_loop_wtd);
    
        ctx->timer.loop_counter++;
        ctx->timer.time += ctx->timer.loop_period/1000;
        return EMU_RESULT_OK();
    } else {
        ctx->wtd.wtd_triggered = 1;
        ctx->timer.loop_status = LOOP_HALTED;
        ESP_LOGE(TAG, "One loop wtd triggered, loop took to long to execute");
        return EMU_RESULT_CRITICAL(EMU_ERR_BLOCK_FOR_TIMEOUT, 0xFFFF);
    }
}


