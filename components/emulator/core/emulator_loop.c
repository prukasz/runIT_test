#include "emulator_loop.h"
#include "emulator_logging.h"
#include "emulator_interface.h"
#include "emulator_body.h"
#include "emulator_parse.h"
#include "string.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "EMULATOR_LOOP";

static const uint32_t stack_depth = 10*1024;

/**
*@brief Watchdog structure loops skipped and max skipped before trigger and is triggered flag
*/
typedef struct {
    uint8_t loops_skipped;
    uint8_t max_skipp; 
    bool wtd_triggered;
    bool wtd_active;
} emu_wtd_t; 

/**
 * @brief Timer structure for emulator loop timing and status
 */
typedef struct {
    esp_timer_handle_t timer_handle;
    loop_status_t loop_status;
    uint64_t loop_period;
    uint64_t time;
    uint64_t loop_counter;
} emu_timer_t;

/**
 * @brief Emulator loop handle structure containing semaphores, timer, watchdog, and task handle
 */
typedef struct emu_loop_handle_s {
    SemaphoreHandle_t sem_loop_start;
    SemaphoreHandle_t sem_loop_wtd;
    emu_timer_t timer;
    emu_wtd_t wtd;
    bool can_loop_run;
    TaskHandle_t loop_task_handle;
} emu_loop_def_t;


/**
 * @brief Static pointer to the emulator loop handle this is globally used within the loop functions and hidden from outside
 */
static emu_loop_def_t *loop_handle = NULL;

/**
 * @brief Interrupt handler for the loop timer tick 
 */
static void IRAM_ATTR loop_tick_intr_handler(void *arg) {
    emu_loop_def_t *ctx = (emu_loop_def_t *)arg;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    //calculate running time in ms (time stretching possible)
    ctx->timer.time += (ctx->timer.loop_period / 1000);
    
    //This is WTD check normally this shold run as default 
    if (xSemaphoreTakeFromISR(ctx->sem_loop_wtd, &xHigherPriorityTaskWoken) == pdTRUE) {
        ctx->wtd.loops_skipped = 0;
        ctx->wtd.wtd_triggered = 0;
        ctx->timer.loop_counter++;
        xSemaphoreGiveFromISR(ctx->sem_loop_start, &xHigherPriorityTaskWoken);
    }
    else {
        // Watchdog active, increment skipped loops
        if (ctx->wtd.wtd_active) {
            ctx->wtd.loops_skipped++;
            // Check if max skipped loops exceeded and stoop the loop if so
            if (ctx->wtd.loops_skipped > ctx->wtd.max_skipp) {
                ctx->wtd.wtd_triggered = 1;
                esp_timer_stop(ctx->timer.timer_handle);
                ctx->timer.loop_status = LOOP_HALTED;
            }
        }
    }
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}


emu_result_t emu_loop_init(uint64_t period_us) {
    // Cleanup previous loop if exists this allows reinitialization
    if(loop_handle){
        vSemaphoreDelete(loop_handle->sem_loop_start);
        vSemaphoreDelete(loop_handle->sem_loop_wtd);
        free(loop_handle);
        loop_handle = NULL;
        EMU_REPORT(EMU_LOG_loop_reinitialized, EMU_OWNER_emu_loop_init, 0, TAG, "Previous loop handle existed, reinitializing");
    }
    //now create new loop handle
    loop_handle = calloc(1, sizeof(emu_loop_def_t));
    if (!loop_handle) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to allocate loop handle");}

    // Default settings
    loop_handle->wtd.max_skipp = 2;
    loop_handle->wtd.wtd_active = 1;
    loop_handle->wtd.wtd_triggered = 0;
    
    loop_handle->timer.loop_period = period_us;
    loop_handle->timer.loop_status = LOOP_CREATED;
    
    //and new semaphores
    loop_handle->sem_loop_start = xSemaphoreCreateBinary();
    loop_handle->sem_loop_wtd = xSemaphoreCreateBinary();

    if (!loop_handle->sem_loop_start || !loop_handle->sem_loop_wtd) {
        if (loop_handle->sem_loop_start) vSemaphoreDelete(loop_handle->sem_loop_start);
        if (loop_handle->sem_loop_wtd) vSemaphoreDelete(loop_handle->sem_loop_wtd);
        vTaskDelete(loop_handle->loop_task_handle);
        free(loop_handle);
        loop_handle = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to create semaphores");
    }

    // Create Task for loop execution
    BaseType_t task_res = xTaskCreate(emu_body_loop_task, "EMU_LOOP", stack_depth, NULL, 4, &loop_handle->loop_task_handle);
    if (task_res != pdPASS) {
        vSemaphoreDelete(loop_handle->sem_loop_start);
        vSemaphoreDelete(loop_handle->sem_loop_wtd);
        vTaskDelete(loop_handle->loop_task_handle);
        free(loop_handle);
        loop_handle = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to create loop task");
    }
    
    // Create hardware timer for loop timing
    const esp_timer_create_args_t timer_args = {
        .callback = &loop_tick_intr_handler,
        .arg = (void*)loop_handle,
        .name = "EMU_TIMER",
        .dispatch_method = ESP_TIMER_ISR
    };
    
    //create tiemer and pass handle to loop handle
    esp_err_t err = esp_timer_create(&timer_args, &loop_handle->timer.timer_handle);
    if (err != ESP_OK) {
        // Task cleanup is complex (need task handle), log and fail critically
        vSemaphoreDelete(loop_handle->sem_loop_start);
        vSemaphoreDelete(loop_handle->sem_loop_wtd);
        vTaskDelete(loop_handle->loop_task_handle);
        free(loop_handle);
        loop_handle = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Timer create failed: %s", esp_err_to_name(err));
    }

    EMU_RETURN_OK(EMU_LOG_loop_initialized, EMU_OWNER_emu_loop_init, 0, TAG, "Loop initialized with period %llu us", period_us);
}


emu_result_t emu_loop_start() {
    // Check if loop is initialized
    if (!loop_handle) {
        EMU_RETURN_WARN(EMU_ERR_LOOP_NOT_INITIALIZED, EMU_OWNER_emu_loop_start, 0 ,0, TAG, "Loop not initialized");
    }
    // Check if loop can run (parsed and ready)
    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_loop_start, 0, 0, TAG, "Loop start denied");}

    bool should_start_timer = false;

    if (loop_handle->timer.loop_status == LOOP_CREATED) {
        EMU_REPORT(EMU_LOG_loop_starting, EMU_OWNER_emu_loop_start, 0, TAG, "Starting loop (First Time)");
        loop_handle->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (loop_handle->timer.loop_status == LOOP_STOPPED) {
        EMU_REPORT(EMU_LOG_loop_starting, EMU_OWNER_emu_loop_start, 0, TAG, "Resuming loop (From Stopped)");
        loop_handle->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else if (loop_handle->timer.loop_status == LOOP_HALTED) {
        LOG_I(TAG, "Resuming loop (From Halted)");
        /* Reset WTD flags on resume */
        loop_handle->wtd.wtd_triggered = 0;
        loop_handle->wtd.loops_skipped = 0;
        loop_handle->timer.loop_status = LOOP_RUNNING;
        should_start_timer = true;
    }
    else {
        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_start, 0, 0, TAG, "Loop start requested but state is %d", loop_handle->timer.loop_status);
    }

    if (should_start_timer) {
        xSemaphoreTake(loop_handle->sem_loop_wtd, 0);
        xSemaphoreGive(loop_handle->sem_loop_start);
        esp_err_t err = esp_timer_start_periodic(loop_handle->timer.timer_handle, loop_handle->timer.loop_period);
        if (err != ESP_OK) {
            loop_handle->timer.loop_status = LOOP_STOPPED;
            EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_start, 0, 0, TAG, "Failed to start hardware timer: %s", esp_err_to_name(err));
        }
    }
    
    EMU_RETURN_OK(EMU_LOG_loop_started, EMU_OWNER_emu_loop_start, 0, TAG, "Loop started with period %llu us", loop_handle->timer.loop_period);
}

emu_result_t emu_loop_stop() {
    // Check if loop is initialized
    if (!loop_handle) {
        EMU_RETURN_WARN(EMU_ERR_LOOP_NOT_INITIALIZED, EMU_OWNER_emu_loop_stop, 0 ,0, TAG, "Loop not initialized");
    }
    // Check if loop is running
    if (loop_handle->timer.loop_status != LOOP_RUNNING) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_stop, 0, 0, TAG, "Attempted to stop loop, but state is %d (Not Running)", loop_handle->timer.loop_status);
    }

    ESP_LOGI(TAG, "Stopping loop");
    loop_handle->timer.loop_status = LOOP_STOPPED;

    //check esp timer stop
    if (unlikely(esp_timer_stop(loop_handle->timer.timer_handle) != ESP_OK)) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_stop, 0, 0, TAG, "Failed to stop hardware timer");
    }
    EMU_RETURN_OK(EMU_LOG_loop_stopped, EMU_OWNER_emu_loop_stop, 0, TAG, "Loop stopped successfully");
}

emu_result_t emu_loop_set_period(uint64_t period_us) {
    // Check if loop is initialized
    if (!loop_handle) {
        EMU_RETURN_WARN(EMU_ERR_LOOP_NOT_INITIALIZED, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Handle is NULL");
    }

    bool was_clamped = false;

    if (period_us > LOOP_PERIOD_MAX) {
        EMU_REPORT(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Clamping period %llu -> %llu us (Too Slow)", period_us, (uint64_t)LOOP_PERIOD_MAX);
        period_us = LOOP_PERIOD_MAX;
        was_clamped = true;
    } 
    else if (period_us < LOOP_PERIOD_MIN) {
        EMU_REPORT(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Clamping period %llu -> %llu us (Too Fast)", period_us, (uint64_t)LOOP_PERIOD_MIN);
        period_us = LOOP_PERIOD_MIN;
        was_clamped = true;
    }

    uint64_t old_period = loop_handle->timer.loop_period;
    loop_handle->timer.loop_period = period_us;

    if (loop_handle->timer.loop_status == LOOP_RUNNING) {
        esp_timer_stop(loop_handle->timer.timer_handle);
        
        esp_err_t err = esp_timer_start_periodic(loop_handle->timer.timer_handle, loop_handle->timer.loop_period);
        if (err != ESP_OK) {
            loop_handle->timer.loop_status = LOOP_HALTED;
            EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Failed to restart timer: %d", err);
        }
        
        EMU_REPORT(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Period updated live: %llu -> %llu us", old_period, loop_handle->timer.loop_period);
    } else {
        EMU_REPORT(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Period config updated: %llu -> %llu us (Next start)", old_period, loop_handle->timer.loop_period);
    }

    if (was_clamped) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_loop_set_period, 0, 0, TAG, "Period was clamped to %llu us", loop_handle->timer.loop_period);
    }

    EMU_RETURN_OK(EMU_LOG_loop_period_set, EMU_OWNER_emu_loop_set_period, 0, TAG, "Loop period set to %llu us", loop_handle->timer.loop_period);
}

emu_result_t emu_loop_run_once() {
    if (!loop_handle) { 
        EMU_RETURN_WARN(EMU_ERR_LOOP_NOT_INITIALIZED, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Handle is NULL");
    }

    if (loop_handle->timer.loop_status == LOOP_RUNNING) {
        EMU_RETURN_WARN(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Cannot run_once while loop is RUNNING. Stop it first");
    }

    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if (res.code != EMU_OK) {EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "Loop run_once denied"); }

    xSemaphoreGive(loop_handle->sem_loop_start);

    TickType_t timeout_ticks = pdMS_TO_TICKS((loop_handle->wtd.max_skipp * loop_handle->timer.loop_period) / 1000);
    if (timeout_ticks == 0) timeout_ticks = 1; 
    
    if (xSemaphoreTake(loop_handle->sem_loop_wtd, timeout_ticks) == pdTRUE) {
        loop_handle->timer.loop_counter++;
        loop_handle->timer.time += loop_handle->timer.loop_period / 1000;
        EMU_RETURN_OK(EMU_LOG_loop_ran_once, EMU_OWNER_emu_loop_run_once, 0, TAG, "Loop run_once completed successfully");
    } else {
        loop_handle->wtd.wtd_triggered = 1;
        loop_handle->timer.loop_status = LOOP_HALTED;
        EMU_RETURN_CRITICAL(EMU_ERR_WTD_TRIGGERED, EMU_OWNER_emu_loop_run_once, 0, 0, TAG, "One loop wtd triggered, loop took too long to execute");
    }
}

uint64_t emu_loop_get_time() {
    if (!loop_handle) return 0;
    return loop_handle->timer.time;
}

uint64_t emu_loop_get_iteration() {
    if (!loop_handle) return 0;
    return loop_handle->timer.loop_counter;
}

bool emu_loop_is_running() {
    if (!loop_handle) return false;
    return (loop_handle->timer.loop_status == LOOP_RUNNING);
}

bool emu_loop_is_halted() {
    if (!loop_handle) return false;
    return (loop_handle->timer.loop_status == LOOP_HALTED);
}

bool emu_loop_is_stopped() {
    if (!loop_handle) return false;
    return (loop_handle->timer.loop_status == LOOP_STOPPED);
}

bool emu_loop_is_initialized() {
    return (loop_handle != NULL);
}

bool emu_loop_wtd_status() {
    if (!loop_handle) return false;
    return loop_handle->wtd.wtd_triggered;
}



emu_result_t emu_loop_deinit() {
    if (!loop_handle) {
        EMU_RETURN_WARN(EMU_ERR_LOOP_NOT_INITIALIZED, EMU_OWNER_emu_loop_init, 0 ,0, TAG, "Loop not initialized");
    }
    esp_err_t err = esp_timer_delete(loop_handle->timer.timer_handle);
    if (err != ESP_OK) {
        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_STATE, EMU_OWNER_emu_loop_init, 0, 0, TAG, "Failed to delete timer: %s", esp_err_to_name(err));
    }
    vSemaphoreDelete(loop_handle->sem_loop_start);
    vSemaphoreDelete(loop_handle->sem_loop_wtd);
    vTaskDelete(loop_handle->loop_task_handle);
    free(loop_handle);
    loop_handle = NULL;
    EMU_RETURN_OK(EMU_LOG_loop_stopped, EMU_OWNER_emu_loop_init, 0, TAG, "Loop deinitialized");
}

bool emu_loop_wait_for_cycle_start(BaseType_t ticks_to_wait) {
    if (!loop_handle) {
        return false;
    }
    return xSemaphoreTake(loop_handle->sem_loop_start, ticks_to_wait) == pdTRUE;
}

bool emu_loop_notify_cycle_end() {
    if (!loop_handle) {
        return false;
    }
    xSemaphoreGive(loop_handle->sem_loop_wtd);
    return true;
}

uint8_t emu_loop_get_wtd_max_skipped() {
    if (!loop_handle) {
        return 0;
    }
    return loop_handle->wtd.max_skipp;
}

uint64_t emu_loop_get_period() {
    if (!loop_handle) {
        return 0;
    }
    return loop_handle->timer.loop_period;
}
