#include "emulator_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

QueueHandle_t error_logs_queue_t=NULL; 
QueueHandle_t logs_queue_t=NULL;
SemaphoreHandle_t logger_request_sem = NULL;
SemaphoreHandle_t logger_done_sem = NULL;
TaskHandle_t logger_task_handle = NULL;


//todo implement proper logger task
static const char *TAG = "emulator_logger";

static void vLoggerTask(void *pvParameters){
    emu_result_t error_item;
    #ifdef ENABLE_REPORT
        emu_report_t report_item;
    #endif

    while (1) {
        // Wait until emulator body notifies logger task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Proceed to dump logs
            // Dump all error logs
            while (error_logs_queue_t && xQueueReceive(error_logs_queue_t, &error_item, 0) == pdTRUE) {
                    const char *owner_s = EMU_OWNER_TO_STR(error_item.owner);
                    const char *code_s = EMU_ERR_TO_STR(error_item.code);
                    if (error_item.abort) {
                        ESP_LOGE(TAG, "ERR owner:%s idx:%u code:%s time:%llu cycle:%llu depth:%u abort:%u warn:%u notice:%u",
                                 owner_s, error_item.owner_idx, code_s, error_item.time, error_item.cycle,
                                 error_item.depth, error_item.abort, error_item.warning, error_item.notice);
                    } else if (error_item.warning) {
                        ESP_LOGE(TAG, "ERR owner:%s idx:%u code:%s time:%llu cycle:%llu depth:%u abort:%u warn:%u notice:%u",
                                 owner_s, error_item.owner_idx, code_s, error_item.time, error_item.cycle,
                                 error_item.depth, error_item.abort, error_item.warning, error_item.notice);
                    } else if (error_item.notice) {
                        ESP_LOGI(TAG, "ERR owner:%s idx:%u code:%s time:%llu cycle:%llu depth:%u abort:%u warn:%u notice:%u",
                                 owner_s, error_item.owner_idx, code_s, error_item.time, error_item.cycle,
                                 error_item.depth, error_item.abort, error_item.warning, error_item.notice);
                    } else {
                        ESP_LOGW(TAG, "ERR owner:%s idx:%u code:%s time:%llu cycle:%llu depth:%u abort:%u warn:%u notice:%u",
                                 owner_s, error_item.owner_idx, code_s, error_item.time, error_item.cycle,
                                 error_item.depth, error_item.abort, error_item.warning, error_item.notice);
                    }
            }

            #ifdef ENABLE_REPORT
            // Dump all reports
            while (logs_queue_t && xQueueReceive(logs_queue_t, &report_item, 0) == pdTRUE) {
                ESP_LOGI(TAG, "RPT %s owner:%s idx:%u time:%llu cycle:%llu",
                         EMU_LOG_TO_STR(report_item.log), EMU_OWNER_TO_STR(report_item.owner), report_item.owner_idx,
                         report_item.time, report_item.cycle);
            }
            #endif

            // Signal done via semaphore (kept for completion signaling)
            if (logger_done_sem) {
                xSemaphoreGive(logger_done_sem);
            }
    }
}


//todo finish init function
BaseType_t logger_task_init(void) {
    error_logs_queue_t = xQueueCreate(LOG_QUEUE_SIZE, sizeof(emu_result_t));

    #ifdef ENABLE_REPORT
    logs_queue_t        = xQueueCreate(REPORT_QUEUE_SIZE, sizeof(emu_report_t));
    #endif

    logger_request_sem = xSemaphoreCreateBinary();
    logger_done_sem = xSemaphoreCreateBinary();

    if (error_logs_queue_t == NULL || logger_request_sem == NULL || logger_done_sem == NULL) {
        return pdFAIL;
    }

    // Create logger task
    if (xTaskCreate(vLoggerTask, "emu_logger", 2048, NULL, tskIDLE_PRIORITY + 1, &logger_task_handle) != pdPASS) {
        return pdFAIL;
    }

    return pdPASS;
}
