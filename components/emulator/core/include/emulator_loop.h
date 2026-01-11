#pragma once
#include "esp_timer.h"
#include "emulator_types.h"

#include "freertos/semphr.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000



/**
* @brief start loop if possible, else return error
*/
emu_result_t emu_loop_start(emu_loop_handle_t *handle);
/**
* @brief stop loop if running, else return error
*/
emu_result_t emu_loop_stop(emu_loop_handle_t *handle);
/**
* @brief create loop and emulator body_loop_task
*/
emu_result_t emu_loop_init(uint64_t period_us, emu_loop_handle_t *out_handle);

/** 
*@brief set emulator tickrate
*/
emu_result_t emu_loop_set_period(emu_loop_handle_t *handle, uint64_t period_us); 

uint64_t emu_loop_get_time(void);
uint64_t emu_loop_get_iteration(void);

