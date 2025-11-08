#pragma once
#include "esp_timer.h"
#include "stdint.h"

#define LOOP_PERIOD_MIN 10000 //us
#define LOOP_PERIOD_MAX 100000 //us

typedef enum {
    EMU_OK = 0,
    EMU_ERR_INVALID_STATE,
    EMU_ERR_INVALID_ARG,
    EMU_ERR_NO_MEMORY,
} emulator_err_t; //ERRORS

typedef enum {
    EMU_NOT_SET,
    EMU_SET,
    EMU_RUNNING,
    EMU_STOPPED,
    EMU_HALTED,
    EMU_FINISHED,
} loop_status_t;

emulator_err_t loop_create_set_period(uint64_t period);
emulator_err_t loop_start(void);
emulator_err_t loop_stop(void);

emulator_err_t emulator_init();
emulator_err_t emulator_start_execution();
emulator_err_t emulator_stop_execution();

void emulator_body_task(void* params);
