#pragma once
#include "esp_timer.h"
#include "emu_types.h"

/*************************************************************************************************************************************************************************
Emulator loop is responsible for timing and execution control of the blocks flow and
I/O read write timing

It also implements a software watchdog timer (WTD) to prevent infinite loops or blocks 
that take too long to execute. It doesn't protect against inifinite loops inside block code.

WTD is configurable in terms of max skipped cycles before triggering.
When WTD is triggered, the loop halts and must be manually restarted.

Max skipped cycles is configurable via emu_loop_set_wtd_max_skipped() and when count below limits,
thn wtd just skips the current invocation, allowing the loop to continue running. Allowing for demanding 
branches to complete without halting the entire emulation.
When max skipped is used then loop time works as if all cycles were executed.

Loop period is configurable via emu_loop_set_period(). The minimum period is defined by LOOP_PERIOD_MIN and maximum by LOOP_PERIOD_MAX.
Loop period is in microseconds. Loop period affects the timing of I/O operations and block execution timing.
Also loop timing affect all time dependent blocks (like timers, clocks, etc). loop period can be changed live while loop is running.
Resolution of timing in blocks is defined by loop period. so if loop period is 100000 us (100 ms) then all time dependent blocks will have resolution of 100 ms.

All loop control functions return emu_result_t structure containing error code and additional info.
Loop struct is opaque and managed internally. User can only interact with it via provided API functions.

************************************************************************************************************************************************************************/

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000


/**
* @brief start loop if possible, else return error
*/
emu_result_t emu_loop_start(void);

/**
* @brief stop loop if running, else return error
*/
emu_result_t emu_loop_stop(void);

/**
* @brief create loop and emulator body_loop_task
* @param period_us Loop period in microseconds
*/
emu_result_t emu_loop_init(uint64_t period_us);

/** 
*@brief set emulator tickrate
*/
emu_result_t emu_loop_set_period(uint64_t period_us); 

/**
* @brief Run loop once (single step execution)
*/
emu_result_t emu_loop_run_once(void);

/**
* @brief Get current loop time in ms
*/
uint64_t emu_loop_get_time(void);

/**
* @brief Get current loop iteration count
*/
uint64_t emu_loop_get_iteration(void);

/**
* @brief Check if loop is currently running
*/
bool emu_loop_is_running(void);

/**
* @brief Check if loop is halted due to watchdog trigger
*/
bool emu_loop_is_halted(void);

/**
* @brief Check if loop is stopped
*/
bool emu_loop_is_stopped(void);

/**
* @brief Check if loop is initialized
*/
bool emu_loop_is_initialized(void);

/**
* @brief Check if watchdog timer was triggered
*/
bool emu_loop_wtd_status(void);

/**
* @brief Deinitialize loop and emulator task 
*/
emu_result_t emu_loop_deinit(void);


/**
 * @brief Get maximum allowed skipped cycles before WTD triggers
 */
uint8_t emu_loop_get_wtd_max_skipped();

/**
 * @brief Get current loop period in microseconds
 */
uint64_t emu_loop_get_period(void);

/**
 * @brief Try to get semaphore wrapper for emulator body loop start
 */
bool emu_loop_wait_for_cycle_start(BaseType_t ticks_to_wait);

/**
 * @brief Notify loop that cycle has ended (give WTD semaphore)
 */
bool emu_loop_notify_cycle_end();
