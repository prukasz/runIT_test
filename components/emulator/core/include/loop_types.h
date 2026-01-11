#pragma  once
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

/**
* @brief emulator main loop flags
*/
typedef enum {
    LOOP_NOT_SET,
    LOOP_CREATED,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

/**
* @brief Loop watchdog struct 
*/

typedef struct {
    uint8_t loops_skipped;
    uint8_t max_skipp; 
    volatile bool wtd_triggered ;
    bool wtd_active;
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
}loop_handle_s;


typedef loop_handle_s *emu_loop_handle_t;