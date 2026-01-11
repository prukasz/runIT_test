#include "emulator_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

QueueHandle_t error_logs_queue_t=NULL; 
QueueHandle_t logs_queue_t=NULL;


//todo implement proper logger task
static void vLoggerTask(void *pvParameters){
    emu_result_t error_item;
    #ifdef ENABLE_REPORT
        emu_report_t report_item;
    #endif

    while (1) {
        vTaskDelay(1000);
    }
}


//todo finish init function
BaseType_t logger_task_init(void) {
    error_logs_queue_t = xQueueCreate(LOG_QUEUE_SIZE, sizeof(emu_result_t));
    
    #ifdef ENABLE_REPORT
    logs_queue_t        = xQueueCreate(REPORT_QUEUE_SIZE, sizeof(emu_report_t));
    #endif

    if (error_logs_queue_t == NULL) {
        return pdFAIL;
    }

    return pdPASS;
}
