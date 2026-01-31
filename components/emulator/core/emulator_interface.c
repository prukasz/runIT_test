#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emulator_interface.h"
#include "emulator_body.h"
#include "emulator_blocks.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_variables_acces.h"
#include "emulator_types.h"
#include "emulator_parse.h"
#include "emulator_logging.h"

static const char * TAG = __FILE_NAME__;

chr_msg_buffer_t *source = NULL; // Source buffer pointer

/* ORDERS QUEUE */
QueueHandle_t emu_interface_orders_queue;


/* ============================================================================
    MAIN INTERFACE TASK
   ============================================================================ */

void emu_interface_task(void* params){
    emu_result_t res = {.code = EMU_OK};

    ESP_LOGI(TAG, "Emulator interface task started");
    
    // Initialize logging queues FIRST - required for error macros
    if (logger_task_init() != pdPASS) {
        ESP_LOGE(TAG, "Failed to create logging queues");
        vTaskDelete(NULL);
    }
    
    // Create Queue
    emu_interface_orders_queue = xQueueCreate(10, sizeof(emu_order_t));
    if (!emu_interface_orders_queue) {
        ESP_LOGE(TAG, "Failed to create orders queue");
        vTaskDelete(NULL);
    }

    static emu_order_t current_order;
    mem_access_allocate_space(1000, 500);
    while(true){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &current_order, portMAX_DELAY)){
            
            ESP_LOGI(TAG, "Processing order: 0x%04X", current_order);
            
            if ((current_order & 0xFF00) == 0xAA00){
                LOG_I(TAG, "Parser order detected: 0x%04X", current_order);
                res = emu_parse_manager(source, current_order, emu_get_current_code_ctx(), NULL);
                continue;
            }    

            switch (current_order){     
                
                case ORD_EMU_LOOP_INIT:
                    res = emu_loop_init(1000000);
                    break;

                case ORD_EMU_LOOP_START:
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
                    chr_msg_buffer_clear(source);
                    emu_reset_code_ctx();
                    break;

                case ORD_RESET_BLOCKS:
                    res = emu_loop_stop();
                    emu_reset_code_ctx();
                    // Also restart parser block stage?
                    break;

                case ORD_RESET_MGS_BUF:
                    ESP_LOGI(TAG, "Clearing Msg buffer");   
                    chr_msg_buffer_clear(source);
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown order: 0x%04X", current_order);
                    break;
            }

            if (res.code != EMU_OK && res.abort) {
                ESP_LOGE(TAG, "Order 0x%04X failed: %s", current_order, EMU_ERR_TO_STR(res.code));
            }
        }
    }
}



#undef OWNER
#define OWNER EMU_OWNER_emu_parse_source_add
emu_result_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (!msg) {RET_E(EMU_ERR_NULL_PTR, "Source is NULL");}
    source = msg;
    RET_OK("Source for parser added");
}