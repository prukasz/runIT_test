#pragma once
#include "esp_timer.h"
#include "emulator_types.h"
#include "emulator_parse.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000

/**
* @brief Loop watchdog struct 
*/


typedef struct {
    uint8_t loops_skipped;
    uint8_t max_skipp; 
    uint8_t wtd_triggered :1;
    uint8_t wtd_active    :1;
    uint8_t reserved      :6;
} emu_wtd_t;

typedef struct {
    esp_timer_handle_t timer_handle;
    loop_status_t loop_status;
    uint64_t loop_period;
    uint64_t time;
    uint64_t loop_counter;
} emu_timer_t;

typedef struct{
    SemaphoreHandle_t sem_loop_start;
    SemaphoreHandle_t sem_loop_wtd;
    emu_timer_t timer;
    emu_wtd_t wtd;
    bool can_loop_run;
}emu_loop_ctx_t;

typedef emu_loop_ctx_t* emu_loop_handle_t;


/**
* @brief start loop if possible, else return error
*/
emu_result_t emu_loop_start(emu_loop_handle_t handle);
/**
* @brief stop loop if running, else return error
*/
emu_result_t emu_loop_stop(emu_loop_handle_t handle);
/**
* @brief create loop and emulator body_loop_task
*/
emu_loop_handle_t emu_loop_init(uint64_t period_us);

/** 
*@brief set emulator tickrate
*/
void emu_loop_set_period(emu_loop_handle_t handle, uint64_t period_us); 
