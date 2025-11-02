#pragma once
#include "esp_timer.h"
#include "emulator.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 100000


typedef enum {
    EMU_NOT_SET,
    EMU_SET,
    EMU_RUNNING,
    EMU_STOPPED,
    EMU_HALTED,
    EMU_FINISHED,
} loop_status_t;

emu_err_t loop_create_set_period(uint64_t period);
emu_err_t loop_start(void);
emu_err_t loop_stop(void);
emu_err_t loop_init();
emu_err_t loop_start_execution();
emu_err_t loop_stop_execution();

void emulator_body_task(void* params);
