#pragma once
#include "esp_timer.h"
#include "emulator_types.h"
#include "emulator.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000

/**
* @brief loop watchdog struct 
*/
typedef struct{
    uint8_t loops_skipped;
    uint8_t max_skipp; 
    bool wtd_triggered;
}emu_wtd_t;

/**
* @brief loop watchdog struct 
*/
typedef struct{
    loop_status_t loop_status;
    emu_wtd_t wtd;
    uint64_t loop_counter;
    bool can_run;
}emu_status_t;

/*loop watchdog*/
extern emu_status_t status;
#define WTD_SET()        (status.wtd.wtd_triggered = true)
#define WTD_RESET()      (status.wtd.wtd_triggered = false, status.wtd.loops_skipped = 0)
#define WTD_SET_LIMIT(x) (status.wtd.max_skipp = (uint8_t)(x))
#define WTD_CNT_UP()     (status.wtd.loops_skipped++)
#define WTD_EXCEEDED()   (status.wtd.loops_skipped > status.wtd.max_skipp)

#define LOOP_CNT_UP()         (status.loop_counter++)
#define LOOP_CNT_REST()       (status.loop_counter = 0)
#define LOOP_SET_STATUS(x)    (status.loop_status = (x))
#define LOOP_STATUS_CMP(x)    (status.loop_status == (x))
#define LOOP_SET_RUN(x)       (status.can_run = (x))
#define LOOP_CAN_RUN()        (status.can_run)

/**
* @brief start loop if possible, else return error
*/
emu_err_t emu_loop_start(void);
/**
* @brief stop loop if running, else return error
*/
emu_err_t emu_loop_stop(void);
/**
* @brief create loop and emulator body_loop_task
*/
emu_err_t emu_loop_init(void);

/**
* @brief execute wrapper for emu_loop_start
*/
emu_err_t emu_execute_loop_start_execution(void);

/**
* @brief execute wrapper for emu_loop_stop
*/
emu_err_t emu_execute_loop_stop_execution(void);

/** 
*@brief set emulator tickrate
*/
void emu_loop_period_set(uint64_t period_us); 