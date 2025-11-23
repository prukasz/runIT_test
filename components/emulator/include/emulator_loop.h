#pragma once
#include "esp_timer.h"
#include "emulator_types.h"
#include "emulator.h"

#define LOOP_PERIOD_MIN 10000
#define LOOP_PERIOD_MAX 1000000

typedef struct{
    bool wtd_triggered;
    uint8_t loops_skipped;
    uint8_t max_skipp; 
}emu_wtd_t;

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

emu_err_t loop_start(void);
emu_err_t loop_stop(void);
emu_err_t loop_init(void);
emu_err_t loop_start_execution(void);
emu_err_t loop_stop_execution(void);
void loop_set_period(uint64_t period_us);

void loop_task(void* params);
