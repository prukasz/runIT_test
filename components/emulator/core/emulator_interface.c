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

static emu_err_t _interface_execute_loop_init(void);
static emu_err_t _interface_execute_loop_stop_execution(void);
static emu_err_t _interface_execute_loop_start_execution(void);

/***************************************************************************/

extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;

void emu_interface_task(void* params){
    ESP_LOGI(TAG, "Emulator interface task created");
    emu_interface_orders_queue  = xQueueCreate(5, sizeof(emu_order_t));
    static emu_order_t orders;
    emu_parse_manager(PARSE_RESTART);
    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "Received order: 0x%04X" , orders);
            switch (orders){    
                case ORD_PARSE_VARIABLES:
                     emu_parse_manager(PARSE_CREATE_VARIABLES);
                    break;
                case ORD_PARSE_INTO_VARIABLES:
                     emu_parse_manager(PARSE_FILL_VARIABLES);
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
                
                    emu_parse_manager(PARSE_RESTART);
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
                    emu_parse_manager(PARSE_CREATE_BLOCKS_LIST);
                    emu_parse_manager(PARSE_CREATE_BLOCKS);
                    emu_parse_manager(PARSE_FILL_BLOCKS);
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
static emu_err_t _interface_execute_loop_init(void){
    if(EMU_OK==emu_parse_manager(PARSE_CHEKC_CAN_RUN)){
        emu_err_t err = emu_loop_init();
        if (EMU_OK!= err){
            ESP_LOGE(TAG, "While creating loop error: %d", err);
            return err;
        }
    }else{
        ESP_LOGE(TAG, "Can't init loop, first parse code");
        return EMU_ERR_ORD_CANNOT_EXECUTE;
    }
    return EMU_OK;
}   


/** 
* @brief Add source of data for parser
*/
emu_err_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        return EMU_ERR_NULL_PTR;   
    }
    source = msg;
    return EMU_OK;
}

