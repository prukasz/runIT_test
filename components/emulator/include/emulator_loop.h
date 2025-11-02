#pragma once
#include "esp_timer.h"
#include "emulator.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 100000


typedef enum {
    LOOP_NOT_SET,
    LOOP_SET,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

emu_err_t loop_start(void);
emu_err_t loop_stop(void);
emu_err_t loop_init(void);
emu_err_t loop_start_execution(void);
emu_err_t loop_stop_execution(void);

void loop_task(void* params);
