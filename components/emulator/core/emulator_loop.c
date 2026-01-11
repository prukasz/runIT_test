#include "emulator_loop.h"
#include "emulator_logging.h"
#include "emulator_interface.h"
#include "emulator_body.h"
#include "emulator_parse.h"
#include "string.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "EMULATOR_LOOP";

/**
*@brief loop tick speed 
*/
static uint64_t loop_period_us = 100000;
static const uint32_t stack_depth = 10*1024;

static void IRAM_ATTR loop_tick_intr_handler(void *arg) {
    emu_loop_handle_t *ctx = (emu_loop_handle_t *)arg;
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


emu_result_t emu_loop_init(uint64_t period_us, emu_loop_handle_t *out_handle) {
    if (!out_handle) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Output handle pointer is NULL");
    }
    emu_loop_handle_t *ctx = calloc(1, sizeof(emu_loop_handle_t));
    if (!ctx) {
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to allocate loop handle");
    }

    // Default settings
    ctx->wtd.max_skipp = 2;
    ctx->wtd.wtd_active = 1;
    ctx->wtd.wtd_triggered = 0;
    
    ctx->timer.loop_period = period_us;
    ctx->timer.loop_status = LOOP_CREATED;
    
    ctx->sem_loop_start = xSemaphoreCreateBinary();
    ctx->sem_loop_wtd = xSemaphoreCreateBinary();

    if (!ctx->sem_loop_start || !ctx->sem_loop_wtd) {
        free(ctx);
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to create semaphores");
    }

    // Create Task
    BaseType_t task_res = xTaskCreate(emu_body_loop_task, "EMU_LOOP", stack_depth, (void*)ctx, 4, NULL);
    if (task_res != pdPASS) {
        free(ctx);
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to create loop task");
    }

    // Create Timer
    const esp_timer_create_args_t timer_args = {
        .callback = &loop_tick_intr_handler,
        .arg = (void*)ctx,
        .name = "EMU_TIMER",
        .dispatch_method = ESP_TIMER_ISR
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &ctx->timer.timer_handle);
    if (err != ESP_OK) {
        // Cleanup would be complex here (deleting task), usually critical failure
        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Timer create failed: %d", err);
    }

    // Do NOT give sem_loop_wtd here. It starts empty.
    
    //*out_handle = ctx;
    EMU_RETURN_OK(EMU_LOG_loop_initialized, EMU_OWNER_emu_loop_init, 0, TAG, "Loop initialized with period %llu us", period_us);
}


emu_result_t emu_loop_start(emu_loop_handle_t *handle) {
    if (!handle) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_loop_start, 0 ,0, TAG, "Handle is NULL");
    }
    
    emu_loop_handle_t *ctx = (emu_loop_handle_t *)handle;
    
    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {
        EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_loop_start, 0, 0, TAG, "Loop start denied");
    }

    bool should_start_timer = false;

    if (ctx->timer.loop_status == LOOP_CREATED) {
        EMU_REPORT(EMU_LOG_loop_starting, EMU_OWNER_emu_loop_start, 0, TAG, "Starting loop (First Time)");
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (ctx->timer.loop_status == LOOP_STOPPED) {
        EMU_REPORT(EMU_LOG_loop_starting, EMU_OWNER_emu_loop_start, 0, TAG, "Resuming loop (From Stopped)");
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (ctx->timer.loop_status == LOOP_HALTED) {
        LOG_I(TAG, "Resuming loop (From Halted)");
        /* Reset WTD flags on resume */
        ctx->wtd.wtd_triggered = 0;
        ctx->wtd.loops_skipped = 0;
        ctx->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else {
        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_start, 0, 0, TAG, "Loop start requested but state is %d", ctx->timer.loop_status);
    }

    if (should_start_timer) {
        xSemaphoreTake(ctx->sem_loop_wtd, 0);
        xSemaphoreGive(ctx->sem_loop_start);
        esp_timer_start_periodic(ctx->timer.timer_handle, ctx->timer.loop_period);
    }
    
    EMU_RETURN_OK(EMU_LOG_loop_started, EMU_OWNER_emu_loop_start, 0, TAG, "Loop started with period %llu us", ctx->timer.loop_period);
}

emu_result_t emu_loop_stop(emu_loop_handle_t *handle) {
    if (!handle) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_loop_stop, 0, 0, TAG, "Handle is NULL");
    }
    emu_loop_handle_t *ctx = (emu_loop_handle_t *)handle;

    if (ctx->timer.loop_status != LOOP_RUNNING) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_stop, 0, 0, TAG, "Attempted to stop loop, but state is %d (Not Running)", ctx->timer.loop_status);
    }

    ESP_LOGI(TAG, "Stopping loop");
    ctx->timer.loop_status = LOOP_STOPPED;

    if (unlikely(esp_timer_stop(ctx->timer.timer_handle)) != ESP_OK) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_stop, 0, 0, TAG, "Failed to stop hardware timer");
    }
    EMU_RETURN_OK(EMU_LOG_loop_stopped, EMU_OWNER_emu_loop_stop, 0, TAG, "Loop stopped successfully");
}

emu_result_t emu_loop_set_period(emu_loop_handle_t *handle, uint64_t period_us) {
    if (!handle) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Handle is NULL");
    }

    emu_loop_handle_t *ctx = (emu_loop_handle_t *)handle;
    bool was_clamped = false;

    if (period_us > LOOP_PERIOD_MAX) {
        LOG_W(TAG, "Clamping period %llu -> %d us (Too Slow)", period_us, LOOP_PERIOD_MAX);
        period_us = LOOP_PERIOD_MAX;
        was_clamped = true;
    } 
    else if (period_us < LOOP_PERIOD_MIN) {
        LOG_W(TAG, "Clamping period %llu -> %d us (Too Fast)", period_us, LOOP_PERIOD_MIN);
        period_us = LOOP_PERIOD_MIN;
        was_clamped = true;
    }

    uint64_t old_period = ctx->timer.loop_period;
    ctx->timer.loop_period = period_us;

    if (ctx->timer.loop_status == LOOP_RUNNING) {
        esp_timer_stop(ctx->timer.timer_handle);
        
        esp_err_t err = esp_timer_start_periodic(ctx->timer.timer_handle, ctx->timer.loop_period);
        if (err != ESP_OK) {
            ctx->timer.loop_status = LOOP_HALTED;
            EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Failed to restart timer: %d", err);
        }
        
        LOG_I(TAG, "Period updated live: %llu -> %llu us", old_period, ctx->timer.loop_period);
    } else {
        LOG_I(TAG, "Period config updated: %llu -> %llu us (Next start)", old_period, ctx->timer.loop_period);
    }

    if (was_clamped) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Period was clamped to %llu us", ctx->timer.loop_period);
    }

    EMU_RETURN_OK(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Loop period set to %llu us", ctx->timer.loop_period);
}

emu_result_t emu_loop_run_once(emu_loop_handle_t *handle) {
    if (!handle) { 
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Handle is NULL");
    }

    emu_loop_handle_t *ctx = (emu_loop_handle_t *)handle;

    if (ctx->timer.loop_status == LOOP_RUNNING) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Cannot run_once while loop is RUNNING. Stop it first");
    }

    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {
        EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Loop run_once denied"); 
    }

    xSemaphoreGive(ctx->sem_loop_start);

    if (xSemaphoreTake(ctx->sem_loop_wtd, pdMS_TO_TICKS(ctx->wtd.max_skipp *loop_period_us)) == pdTRUE) {

        ctx->timer.loop_counter++;
        ctx->timer.time += ctx->timer.loop_period/1000;
        EMU_RETURN_OK(EMU_LOG_loop_ran_once, EMU_OWNER_emu_loop_run_once, 0, TAG, "Loop run_once completed successfully");

    } else {
        ctx->wtd.wtd_triggered = 1;
        ctx->timer.loop_status = LOOP_HALTED;
        EMU_RETURN_CRITICAL(EMU_ERR_WTD_TRIGGERED, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "One loop wtd triggered, loop took to long to execute");
        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_WTD_TRIGGERED, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "One loop wtd triggered, loop took to long to execute");
    }
    EMU_RETURN_OK(EMU_LOG_loop_ran_once, EMU_OWNER_emu_loop_run_once, 0, TAG, "Loop run_once completed successfully");
}

uint64_t emu_loop_get_time(void){
    return 0;
}
uint64_t emu_loop_get_iteration(void){
    return 0;
}