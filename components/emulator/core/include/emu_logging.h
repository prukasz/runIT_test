#pragma once 
#include "error_types.h"
#include "emu_types_info.h"
#include "error_macros.h"
#include "esp_log.h"
#include "emu_logs_config.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "inttypes.h"

/*********************************************************************************************** *
IDEA: we have 2 structs for logging: emu_result_t for errors and emu_report_t for reports / success logs
We have 2 ring buffers: error_logs_buff_t and status_logs_buff_t (if ENABLE_STATUS_BUFF is defined)

Now we can read errors directly via serial console (ESP_LOGx) or send them via BLE to app 

We can also read reports if ENABLE_STATUS_BUFF is defined then we can send reports to app as well 

LIVEDEBUGGING: We can enable ENABLE_LOG_X_FROM_ERROR_MACROS and ENABLE_LOG_X_FROM_STATUS_MACROS to have extra logs in serial console for debugging purposes

*****************************************************************************************************/



extern RingbufHandle_t error_logs_buff_t; 
extern RingbufHandle_t status_logs_buff_t;
extern SemaphoreHandle_t logger_request_sem;
extern SemaphoreHandle_t logger_done_sem;
extern TaskHandle_t logger_task_handle;

// Logger Task Initialization
BaseType_t logger_task_init(void);

