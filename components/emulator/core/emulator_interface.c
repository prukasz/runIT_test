#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emulator_interface.h"
#include "emulator_errors.h"
#include "emulator_body.h"
#include "emulator_blocks.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_variables_acces.h"
#include "emulator_types.h"
#include "emulator_parse.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char * TAG = "EMU_INTERFACE";

/* DATA SOURCES & GLOBALS */
chr_msg_buffer_t *source = NULL; // Source buffer pointer

/* ORDERS QUEUE */
QueueHandle_t emu_interface_orders_queue;

/* EXTERNS */
extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;

/* INTERNAL STATE */
volatile emu_loop_handle_t loop_handle = NULL;

/* FORWARD DECLARATIONS */
static emu_loop_handle_t _interface_execute_loop_init(uint64_t period_us);
static emu_result_t _interface_execute_loop_stop_execution(emu_loop_handle_t loop_handle);
static emu_result_t _interface_execute_loop_start_execution(emu_loop_handle_t loop_handle);

/* ============================================================================
    MAIN INTERFACE TASK
   ============================================================================ */

void emu_interface_task(void* params){
    emu_result_t res = {.code = EMU_OK};

    ESP_LOGI(TAG, "Emulator interface task started");
    
    // Create Queue
    emu_interface_orders_queue = xQueueCreate(10, sizeof(emu_order_t));
    if (!emu_interface_orders_queue) {
        ESP_LOGE(TAG, "Failed to create orders queue");
        vTaskDelete(NULL);
    }

    static emu_order_t current_order;
    
    // Initial Reset
    emu_parse_manager(PARSE_RESTART);
    emu_refs_system_init(1000, 5000);

    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &current_order, portMAX_DELAY)){
            
            ESP_LOGD(TAG, "Processing order: 0x%04X", current_order);
            
            switch (current_order){    
                
                // --- 1. VARIABLES (GLOBAL & BLOCK OUTPUTS) ---
                case ORD_PARSE_VARIABLES:
                    // Allocate memory contexts (Globals=0, Blocks=1, etc.)
                    // Packet: [H_SIZES] [CtxID] ...
                    res = emu_parse_manager(PARSE_CREATE_VARIABLES);
                    break;

                case ORD_PARSE_INTO_VARIABLES:
                    // Fill values into allocated memory
                    // Can be called multiple times
                    res = emu_parse_manager(PARSE_FILL_VARIABLES);
                    break;            

                // --- 2. BLOCKS ---
                case ORD_EMU_CREATE_BLOCK_LIST:
                    // Parse total count [B000] and allocate pointer array
                    res = emu_parse_manager(PARSE_CREATE_BLOCKS_LIST);
                    break;

                case ORD_EMU_CREATE_BLOCKS:
                    // Parse config [BB 00] and allocate structs + configure inputs/outputs
                    res = emu_parse_manager(PARSE_CREATE_BLOCKS);
                    break;
                case ORD_EMU_FILL_BLOCKS:
                    // Parse custom data [BB 01/02] (Math/Logic/For etc.)
                    // Can be called multiple times
                    res = emu_parse_manager(PARSE_FILL_BLOCKS);
                    break;

                // --- 3. LOOP CONTROL ---
                case ORD_EMU_LOOP_INIT:
                    // Check if ready and init timer
                    if (loop_handle) {
                        ESP_LOGW(TAG, "Loop already initialized");
                    } else {
                        loop_handle = _interface_execute_loop_init(100000); // 1s default
                    }
                    break;

                case ORD_EMU_LOOP_START:
                    _interface_execute_loop_start_execution(loop_handle);
                    break;

                case ORD_EMU_LOOP_STOP:
                    _interface_execute_loop_stop_execution(loop_handle);
                    break;

                // --- 4. UTILITY ---
                case ORD_RESET_ALL: 
                    ESP_LOGI(TAG, "RESET ALL ORDER");
                    _interface_execute_loop_stop_execution(loop_handle);
                    loop_handle = NULL;
                    
                    chr_msg_buffer_clear(source);
                    
                    // Free Blocks
                    emu_blocks_free_all(emu_block_struct_execution_list, emu_block_total_cnt);
                    emu_block_struct_execution_list = NULL;
                    emu_block_total_cnt = 0;
                    
                    // Free Memory Contexts (Helper needed or manual loop)
                    // TODO: Iterate and free s_mem_contexts
                    
                    res = emu_parse_manager(PARSE_RESTART);
                    break;

                case ORD_RESET_BLOCKS:
                    _interface_execute_loop_stop_execution(loop_handle);
                    emu_blocks_free_all(emu_block_struct_execution_list, emu_block_total_cnt);
                    emu_block_struct_execution_list = NULL;
                    emu_block_total_cnt = 0;
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
                ESP_LOGE(TAG, "Order 0x%04X failed: %d", current_order, res.code);
            }
        }
    }
}

/* ============================================================================
    HELPER FUNCTIONS
   ============================================================================ */

static emu_result_t _interface_execute_loop_start_execution(emu_loop_handle_t loop_handle){
    if (!loop_handle) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0);
    return emu_loop_start(loop_handle);
}  

static emu_result_t _interface_execute_loop_stop_execution(emu_loop_handle_t loop_handle){
    if (!loop_handle) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0);
    return emu_loop_stop(loop_handle);
}   

static emu_loop_handle_t _interface_execute_loop_init(uint64_t period_us){
    // 1. Verify if system is ready (parsed & linked)
    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    
    if(res.code == EMU_OK){
        emu_loop_handle_t h = emu_loop_init(period_us);
        if (!h) {
            ESP_LOGE(TAG, "Loop init failed");
            return NULL;
        }
        ESP_LOGI(TAG, "Loop initialized");
        return h;
    } else {
        ESP_LOGE(TAG, "Cannot init loop: System not ready/parsed");
        return NULL;
    }
}   

emu_result_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (!msg) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0);
    source = msg;
    return EMU_RESULT_OK();
}