#pragma once
#include "esp_timer.h"
#include "emulator_types.h"
#include "emulator_parse.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000

/**
* @brief Loop watchdog struct 
*/
typedef struct{
    uint8_t loops_skipped;
    uint8_t max_skipp; 
    bool wtd_triggered;
}emu_wtd_t;

/**
* @brief Loop watchdog status struct 
*/
typedef struct{
    loop_status_t loop_status;
    emu_wtd_t wtd;
    uint64_t loop_counter;
    bool can_run;
}emu_status_t;

/************************************************************************************************************************/
/*                                                      Macros for wtd management                                       */

extern emu_status_t emu_global_status;
//Set that wtd is triggered
#define WTD_SET()        (emu_global_status.wtd.wtd_triggered = true)
//Is wtd triggered?
#define WTD_TRIGGERED()  (emu_global_status.wtd.wtd_triggered)
//Reset wtd counter and trigger
#define WTD_RESET()      (emu_global_status.wtd.wtd_triggered = false, emu_global_status.wtd.loops_skipped = 0)
//Set limit of max loop skipps
#define WTD_SET_LIMIT(x) (emu_global_status.wtd.max_skipp = (uint8_t)(x))
//Increment skipped loops
#define WTD_CNT_UP()     (emu_global_status.wtd.loops_skipped++)
//Is limit od skipped tasks excedded?
#define WTD_EXCEEDED()   (emu_global_status.wtd.loops_skipped > emu_global_status.wtd.max_skipp)

//Increment already executed loops
#define LOOP_CNT_UP()         (emu_global_status.loop_counter++)
//Reset already executed loops 
#define LOOP_CNT_REST()       (emu_global_status.loop_counter = 0)
//Set currnet loop status (loop_status_t)
#define LOOP_SET_STATUS(x)    (emu_global_status.loop_status = (x))
//Is current loop status == x (loop_status_t)
#define LOOP_STATUS_CMP(x)    (emu_global_status.loop_status == (x))
//Set can loop run
#define LOOP_SET_RUN(x)       (emu_global_status.can_run = (x))
//Can loop run right now? 
#define LOOP_CAN_RUN()        (emu_global_status.can_run)
/************************************************************************************************************************/
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
*/
emu_result_t emu_loop_init(void);

/** 
*@brief set emulator tickrate
*/
void emu_loop_period_set(uint64_t period_us); 
