#pragma once 
#include "error_types.h"
#include "types_info.h"
#include "error_macros.h"
#include "esp_log.h"
#include "emulator_logs_config.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*********************************************************************************************** *
IDEA: we have 2 structs for logging: emu_result_t for errors and emu_report_t for reports / success logs
We have 2 queues: error_logs_queue_t and logs_queue_t (if ENABLE_REPORT is defined)

Now we can read errors directly via serial console (ESP_LOGx) or sed them via BLE to app 

W can also read reports if ENABLE_REPORT is defined then we can send reports to app as well 

LIVEDEBBUGING: WE can enable ENABLE_DEBUG_LOGS to have extra logs in serial console for debugging purposes

*****************************************************************************************************/



extern QueueHandle_t error_logs_queue_t; 
extern QueueHandle_t logs_queue_t;
extern SemaphoreHandle_t logger_request_sem;
extern SemaphoreHandle_t logger_done_sem;
extern TaskHandle_t logger_task_handle;

// Logger Task Initialization
BaseType_t logger_task_init(void);
