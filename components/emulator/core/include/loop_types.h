#pragma  once
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

/**
* @brief Those are loop status flags that define current state of emulator loop
*/
typedef enum {
    LOOP_NOT_SET,
    LOOP_CREATED,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;



