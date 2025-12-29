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
#include "emulator_types.h"
#include "emulator_parse.h"
#include "esp_heap_caps.h"

static const char * TAG = "EMULATOR INTERFACE";

/********************************************/
/**
*@brief Data source for parsers 
*/
chr_msg_buffer_t *source;
/**
 * @brief Global emulatr memory struct
 */
emu_mem_t mem;
/********************************************/


/**********************************************************************/
/**
 * @brief This queue is used in ble callback to give orders
 */
QueueHandle_t emu_interface_orders_queue;
/**********************************************************************/

/***************************************************************************/
/*             Functions that are executed when order comes in             */

static emu_loop_handle_t _interface_execute_loop_init(uint64_t period_us);
static emu_result_t _interface_execute_loop_stop_execution(emu_loop_handle_t loop_handle);
static emu_result_t _interface_execute_loop_start_execution(emu_loop_handle_t loop_handle);

/***************************************************************************/

extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;

emu_loop_handle_t loop_handle = NULL;

void emu_interface_task(void* params){
    emu_result_t res = {.code = EMU_OK};

    ESP_LOGI(TAG, "Emulator interface task created");
    emu_interface_orders_queue  = xQueueCreate(5, sizeof(emu_order_t));
    static emu_order_t orders;
    res = emu_parse_manager(PARSE_RESTART);
    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "Received order: 0x%04X" , orders);
            switch (orders){    
                case ORD_PARSE_VARIABLES:
                    res = emu_parse_manager(PARSE_CREATE_VARIABLES);
                    break;
                case ORD_PARSE_INTO_VARIABLES:
                    res = emu_parse_manager(PARSE_FILL_VARIABLES);
                    break;             
                case ORD_EMU_LOOP_START:
                    
                    _interface_execute_loop_start_execution(loop_handle);
                    //add here check if code can be run
                    break;
                case ORD_EMU_LOOP_INIT:
                    loop_handle = _interface_execute_loop_init(1000000);
                    if(!loop_handle)
                    {
                        ESP_LOGE(TAG, "While creating loop error");
                    }
                    break;

                case ORD_EMU_LOOP_STOP:
                    _interface_execute_loop_stop_execution(loop_handle);
                    break;

                    case ORD_RESET_ALL: 
                    _interface_execute_loop_stop_execution(loop_handle);
                    chr_msg_buffer_clear(source);
                    emu_variables_reset(&mem);
                    emu_blocks_free_all(emu_block_struct_execution_list, emu_block_total_cnt);
                    emu_block_total_cnt = 0 ;
                
                    res = emu_parse_manager(PARSE_RESTART);
                    break;
                case ORD_RESET_BLOCKS:
                    emu_blocks_free_all(emu_block_struct_execution_list, emu_block_total_cnt);
                    emu_block_total_cnt = 0;
                    break;
                case ORD_RESET_MGS_BUF:
                    ESP_LOGI(TAG, "Clearing Msg buffer");   
                    chr_msg_buffer_clear(source);
                    break;
                case ORD_EMU_CREATE_BLOCK_LIST:
                    res = emu_parse_manager(PARSE_CREATE_BLOCKS_LIST);
                    break;
                case ORD_EMU_CREATE_BLOCKS:
                    res = emu_parse_manager(PARSE_CREATE_BLOCKS);
                    break;
                case ORD_EMU_FILL_BLOCKS:
                    res =emu_parse_manager(PARSE_FILL_BLOCKS);
                    break;
                default:
                    // Unknown order
                    break;
            }
        }
    }
};


/**
*@brief Execute order from user: Start loop execution cycle
*/
static emu_result_t _interface_execute_loop_start_execution(emu_loop_handle_t loop_handle){
    emu_result_t res = {.code = EMU_OK};
    if (!loop_handle){
        LOG_E(TAG, "No loop handle provided");
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    res = emu_loop_start(loop_handle);
    if(res.code!=EMU_OK){
        ESP_LOGE(TAG, "While starting loop error: %s", EMU_ERR_TO_STR(res.code));
        return res;
    }
    return res;
}  

/**
*@brief Execute order from user: Stop loop execution cycle 
*/
static emu_result_t _interface_execute_loop_stop_execution(emu_loop_handle_t loop_handle){
    emu_result_t res = {.code = EMU_OK};
    if (!loop_handle){
        LOG_E(TAG, "No loop handle provided");
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    res = emu_loop_stop(loop_handle);
    if(res.code!=EMU_OK){
        LOG_E(TAG, "While stopping loop error: %s", EMU_ERR_TO_STR(res.code));
        return res;
    }
    return res;
}   //stop and preserve state (stops after loop ends)



/**
*@brief Execute order from user: Create loop timer and emulator body 
*/
static emu_loop_handle_t _interface_execute_loop_init(uint64_t period_us){
    emu_result_t res = emu_parse_manager(PARSE_CHECK_CAN_RUN);
    if(res.code==EMU_OK){
        emu_loop_handle_t loop_handle = emu_loop_init(period_us);
        if (!loop_handle){
            ESP_LOGE(TAG, "While creating loop error");
            return NULL;
        }
        return loop_handle;
    }else{
        ESP_LOGE(TAG, "Can't init loop, first parse code");
        return NULL;
    }
    return NULL;
}   


/** 
* @brief Add source of data for parser
*/
emu_result_t emu_parse_source_add(chr_msg_buffer_t * msg){
    emu_result_t res = {.code = EMU_OK};
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        res.code = EMU_ERR_NULL_PTR;
        res.warning = true;
        return res;   
    }
    source = msg;
    return res;
}

