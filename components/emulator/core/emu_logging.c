#include "emu_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "string.h"

RingbufHandle_t error_logs_buff_t=NULL; 
RingbufHandle_t status_logs_buff_t=NULL;
SemaphoreHandle_t logger_request_sem = NULL;
SemaphoreHandle_t logger_done_sem = NULL;
TaskHandle_t logger_task_handle = NULL;

log_ble_buff_t log_ble_err_buff = {
    .buf = {0},
    .offset = 0
};
log_ble_buff_t log_ble_status_buff = {
    .buf = {0},
    .offset = 0
};

//todo implement proper logger task
static const char *TAG = "emu_logger";

static void vLoggerTask(void *pvParameters){
    emu_result_t error_item;
    #ifdef ENABLE_STATUS_BUFF
        emu_report_t report_item;
    #endif

    while (1) {
        // Wait until emulator body notifies logger task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
            // Dump all error logs
            while (error_logs_buff_t) {
                size_t item_size = 0;
                void *item = xRingbufferReceive(error_logs_buff_t, &item_size, 0);
                if (!item) break;
                if (item_size == sizeof(emu_result_t)) {
                    memcpy(&error_item, item, sizeof(error_item));
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
                logger_add_to_packet(&error_item, sizeof(emu_result_t), &log_ble_err_buff);
                vRingbufferReturnItem(error_logs_buff_t, item);
            }

            // Send any remaining logs in BLE buffer
            if(log_ble_err_buff.offset > 0){
                gatt_send_notify(log_ble_err_buff.buf, log_ble_err_buff.offset);
                log_ble_err_buff.offset = 0;
            }

            #ifdef ENABLE_STATUS_BUFF
            // Dump all reports
            while (status_logs_buff_t) {
                size_t item_size = 0;
                void *item = xRingbufferReceive(status_logs_buff_t, &item_size, 0);
                if (!item) break;
                if (item_size == sizeof(emu_report_t)) {
                    memcpy(&report_item, item, sizeof(report_item));
                    ESP_LOGI(TAG, "RPT %s owner:%s idx:%u time:%llu cycle:%llu",
                             EMU_LOG_TO_STR(report_item.log), EMU_OWNER_TO_STR(report_item.owner), report_item.owner_idx,
                             report_item.time, report_item.cycle);
                }
                logger_add_to_packet(&report_item, sizeof(emu_report_t), &log_ble_status_buff);
                vRingbufferReturnItem(status_logs_buff_t, item);
            }

            if(log_ble_status_buff.offset > 0){
                gatt_send_notify(log_ble_status_buff.buf, log_ble_status_buff.offset);
                log_ble_status_buff.offset = 0;
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
    error_logs_buff_t = xRingbufferCreate(LOG_QUEUE_SIZE * sizeof(emu_result_t), RINGBUF_TYPE_NOSPLIT);

    #ifdef ENABLE_STATUS_BUFF
    status_logs_buff_t        = xRingbufferCreate(REPORT_QUEUE_SIZE * sizeof(emu_report_t), RINGBUF_TYPE_NOSPLIT);
    #endif

    logger_request_sem = xSemaphoreCreateBinary();
    logger_done_sem = xSemaphoreCreateBinary();

    if (error_logs_buff_t == NULL || logger_request_sem == NULL || logger_done_sem == NULL) {
        return pdFAIL;
    }
    ESP_LOGI(TAG, "Logger queues and semaphores created");
    // Create logger task
    if (xTaskCreate(vLoggerTask, "emu_logger", 2048, NULL, tskIDLE_PRIORITY + 1, &logger_task_handle) != pdPASS) {
        return pdFAIL;
    }

    return pdPASS;
}


void logger_add_to_packet(const void *data, size_t size, log_ble_buff_t *buff){
    if (buff->offset + size < sizeof(buff->buf)){
        memcpy(buff->buf + buff->offset, data, size);
        buff->offset += size;
    }
    else
    {
        //send buff via BLE
        gatt_send_notify(buff->buf, buff->offset);
        //reset offset and add new log
        buff->offset = 0;
        memcpy(buff->buf + buff->offset, data, size);
        buff->offset += size;
    }
}
