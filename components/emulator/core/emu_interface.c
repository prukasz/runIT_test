#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emu_interface.h"
#include "emu_body.h"
#include "emu_blocks.h"
#include "emu_loop.h"
#include "emu_variables.h"
#include "emu_variables_acces.h"
#include "emu_parse.h"
#include "emu_logging.h"
#include "emu_buffs.h"

/* Definitions for globals declared extern in emu_buffs.h */

static const char * TAG = __FILE_NAME__;

chr_msg_buffer_t *source = NULL; // Source buffer pointer

/* Callback invoked after each packet is processed (signals BLE peer to send next) */
static void (*packet_done_cb)(void) = NULL;

void emu_interface_set_packet_done_cb(void (*cb)(void)) { packet_done_cb = cb; }
static inline void _notify_done(void) { if (packet_done_cb) packet_done_cb(); }

/* Interface task handle (used for notifications) */
static TaskHandle_t emu_interface_task_handle = NULL;


/* ============================================================================
    MAIN INTERFACE TASK
   ============================================================================ */

void emu_interface_task(void* params){
    emu_result_t res = EMU_RESULT_OK();
    
    ESP_LOGI(TAG, "Emulator interface task started");
    
    // Initialize logging queues FIRST - required for error macros
    if (logger_task_init() != pdPASS) {
        ESP_LOGE(TAG, "Failed to create logging queues");
        vTaskDelete(NULL);
    }
    
    /* store our task handle so other modules can notify us */
    emu_interface_task_handle = xTaskGetCurrentTaskHandle();

    mem_access_allocate_space(1000, 500);

    static uint16_t header;
    static msg_packet_t* in_packet;


    while(true){
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            in_packet = emu_get_in_msg_packet();

            if (in_packet->len < 2) {
                ESP_LOGW(TAG, "Received packet too short to contain anything usefull");
                _notify_done();
                continue;
            }

            /*Detect parse-path packet by checking data[0] against known packet headers*/
            if (emu_is_parse_header(in_packet->data[0])) {
                LOG_I(TAG, "Detected parser packet header: 0x%02X", in_packet->data[0]);
                res = emu_parse_manager(in_packet, 0, emu_get_current_code_ctx(), NULL);
                _notify_done();
                continue;
            }

            memcpy(&header, in_packet->data, sizeof(header));
            ESP_LOGI(TAG, "Processing order: 0x%04X", header);

            switch (header){     
                
                case ORD_EMU_LOOP_INIT:
                    res = emu_loop_init(10000);
                    break;

                case ORD_EMU_LOOP_START:
                    emu_parse_verify_code(emu_get_current_code_ctx());
                    res = emu_loop_start();
                    break;

                case ORD_EMU_LOOP_STOP:
                    res = emu_loop_stop();
                    break;

                // --- 4. UTILITY ---
                case ORD_RESET_ALL: 
                    ESP_LOGI(TAG, "RESET ALL ORDER");
                    res = emu_loop_stop();
                    emu_loop_deinit();
                    emu_reset_code_ctx();
                    break;

                case ORD_RESET_BLOCKS:
                    res = emu_loop_stop();
                    emu_reset_code_ctx();
                    break;


                default:
                    //ESP_LOGW(TAG, "Unknown order: 0x%04X", current_order);
                    break;
            }

            if (res.code != EMU_OK && res.abort) {
                ESP_LOGE(TAG, "Packet with header 0x%02X failed: %s", in_packet->data[0], EMU_ERR_TO_STR(res.code));
            }
            _notify_done();
        
    }
}

/**
 * @brief Notify the interface task that a new packet is ready for processing
 */
BaseType_t emu_interface_process_packet(){
    if (emu_interface_task_handle == NULL) return pdFAIL;
    return xTaskNotifyGive(emu_interface_task_handle);
}
