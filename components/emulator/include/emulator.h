#pragma once
#include "esp_timer.h"
#include "stdint.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 100000

typedef enum {
    LOOP_ERR_OK = 0,
    LOOP_ERR_INVALID_STATE,
} loop_err_t;

typedef enum {
    LOOP_NOT_SET,
    LOOP_SET,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

// function declarations only — no variable definitions
loop_err_t loop_create_set_period(uint64_t period);
loop_err_t loop_start(void);
loop_err_t loop_stop(void);
