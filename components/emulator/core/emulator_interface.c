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

static emu_result_t _interface_execute_loop_init(void);
static emu_err_t _interface_execute_loop_stop_execution(void);
static emu_err_t _interface_execute_loop_start_execution(void);

/***************************************************************************/

extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;

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
                    _interface_execute_loop_start_execution();
                    //add here check if code can be run
                    break;
                case ORD_EMU_LOOP_INIT:
                    _interface_execute_loop_init();
                    break;

                case ORD_EMU_LOOP_STOP:
                    _interface_execute_loop_stop_execution();
                    break;

                    case ORD_RESET_ALL: 
                    _interface_execute_loop_stop_execution();
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
                case ORD_EMU_FILL_BLOCKS_LIST:
                    res = emu_parse_manager(PARSE_CREATE_BLOCKS_LIST);
                    res =emu_parse_manager(PARSE_CREATE_BLOCKS);
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
static emu_err_t _interface_execute_loop_start_execution(void){
    emu_loop_start();
    return EMU_OK;
}  //start execution

/**
*@brief Execute order from user: Stop loop execution cycle 
*/
static emu_err_t _interface_execute_loop_stop_execution(void){
    emu_loop_stop();
    return EMU_OK;
}   //stop and preserve state (stops after loop ends)



/**
*@brief Execute order from user: Create loop timer and emulator body 
*/
static emu_result_t _interface_execute_loop_init(void){
    emu_result_t res = emu_parse_manager(PARSE_CHEKC_CAN_RUN);
    if(res.code==EMU_OK){
        res = emu_loop_init();
        if (res.code!=EMU_OK){
            ESP_LOGE(TAG, "While creating loop error: %s", EMU_ERR_TO_STR(res.code));
            return res;
        }
    }else{
        ESP_LOGE(TAG, "Can't init loop, first parse code");
        res.code = EMU_ERR_PARSE_INVALID_REQUEST;
        res.warning = true;
        return res;
    }
    return res;
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

