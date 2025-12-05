#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emulator_interface.h"
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
static chr_msg_buffer_t *source;
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

static emu_err_t _interface_execute_parse_manager(parse_cmd_t cmd);
static emu_err_t _interface_execute_loop_init(void);
static emu_err_t _interface_execute_loop_stop_execution(void);
static emu_err_t _interface_execute_loop_start_execution(void);

/***************************************************************************/

extern void **emu_global_blocks_structs;
extern emu_block_func *emu_global_blocks_functions;


void emu_interface_task(void* params){
    ESP_LOGI(TAG, "Emulator interface task created");
    emu_interface_orders_queue  = xQueueCreate(5, sizeof(emu_order_t));
    static emu_order_t orders;
    _interface_execute_parse_manager(PARSE_RESTART);
    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "Received order: 0x%04X" , orders);
            switch (orders){    
                case ORD_PARSE_VARIABLES:
                     _interface_execute_parse_manager(PARSE_CREATE_VARIABLES);
                    ESP_LOGI(TAG, "Free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                    break;
                case ORD_PARSE_INTO_VARIABLES:
                     _interface_execute_parse_manager(PARSE_FILL_VARIABLES);
                    ESP_LOGI(TAG, "Free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                    break;
                case ORD_PROCESS_CODE:
                    // Handle processing code
                    break;

                case ORD_CHECK_CODE:
                    // Handle checking completeness
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
                    chr_msg_buffer_clear(source);
                    emu_variables_reset(&mem);
                    emu_execute_blocks_free_all(emu_global_blocks_structs,5);
                
                    _interface_execute_parse_manager(PARSE_RESTART);
                    break;
                case ORD_RESET_BLOCKS:
                    emu_execute_blocks_free_all(emu_global_blocks_structs,5);
                    break;
                case ORD_RESET_MGS_BUF:
                    ESP_LOGI(TAG, "Clearing Msg buffer");   
                    chr_msg_buffer_clear(source);
                    break;

                case ORD_EMU_ALLOCATE_BLOCKS_LIST:
                    emu_create_block_tables(5);
                    ESP_LOGI(TAG, "Free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                    break;
                case ORD_EMU_FILL_BLOCKS_LIST:
                    _interface_execute_parse_manager(PARSE_CREATE_BLOCKS);
                    _interface_execute_parse_manager(PARSE_FILL_BLOCKS);
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
    if(EMU_OK==_interface_execute_parse_manager(PARSE_CHEKC_CAN_RUN)){
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
* @brief Reset flags on parse comleteness status
*/


emu_err_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        return EMU_ERR_NULL_POINTER;   
    }
    source = msg;
    return EMU_OK;
}

/**
 * @brief Use this function to invoke parsing of certain part of code
 * @note Also can check status of parsing
 * @return EMU_OK if status or success, EMU_ERR_DENY when status flase, 
 * EMU_ERR_PARSE_INVALID_REQUEST when can't parse 
 * @param cmd 
 */ 
static emu_err_t _interface_execute_parse_manager(parse_cmd_t cmd){
    static struct {
        bool can_create_variables;
        bool can_fill_variables;
        bool can_create_blocks;
        bool can_fill_blocks;
        bool finished;
        bool can_run_code;
    } flags = {0};

    static struct {
        bool is_create_variables_done;
        bool is_fill_variables_done;
        bool is_create_blocks_done; 
        bool is_fill_blocks_done;
        bool is_parse_finised;
    } status = {0};

    switch (cmd)
    {
    case PARSE_RESTART:
        //clear flags
        memset(&flags, 0, sizeof(flags));
        memset(&status, 0, sizeof(status));
        flags.can_create_variables = true;
        break;
    case PARSE_CREATE_VARIABLES:
        if(flags.can_create_variables  && !status.is_create_variables_done){
            emu_err_t err= emu_parse_variables(source, &mem);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While parsing and creating variables error: %d", err);
                return err;
            }
            flags.can_fill_variables = true; 
            flags.can_create_variables = false;
            status.is_create_variables_done = true;
            ESP_LOGI(TAG, "Succesfuly parsed variables");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't create variables right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_FILL_VARIABLES:
        if(flags.can_fill_variables && status.is_create_variables_done){
            emu_err_t err= emu_parse_variables_into(source, &mem);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While parsing into variables error: %d", err);
                return err;
            }
            if(status.is_fill_variables_done){
                ESP_LOGW(TAG, "Parsing already done reparsing now, some values can be overwritten");
                return EMU_OK;
            }
            flags.can_fill_variables = true;
            flags.can_create_blocks = true;
            status.is_fill_variables_done = true;
            ESP_LOGI(TAG, "Succesfuly filled variables");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't fill variables rigth now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_CREATE_BLOCKS:
        if(flags.can_create_blocks && status.is_create_variables_done){
            emu_err_t err= emu_parse_block(source);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While creating blocks error: %d", err);
                return err;
            }
            flags.can_create_blocks = false;
            flags.can_fill_blocks = true;
            status.is_create_blocks_done = true;
            ESP_LOGI(TAG, "Succesfuly created blocks");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't create blocks right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_FILL_BLOCKS:
        if(flags.can_fill_blocks && status.is_create_blocks_done){
            //dummy
            emu_err_t err= EMU_OK;
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While filling blocks error: %d", err);
                return err;
            }
            flags.can_fill_blocks = false;
            status.is_fill_blocks_done = true;
            flags.can_run_code = true;
            ESP_LOGI(TAG, "Succesfuly filled created blocks");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't fill blocks right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_CHEKC_CAN_RUN:
        if(flags.can_run_code && status.is_fill_blocks_done){
            return EMU_OK;
        }else{
            return EMU_ERR_DENY;
        }
        break;
    
    case PARSE_IS_CREATE_BLOCKS_DONE:
        if(status.is_create_blocks_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;

    case PARSE_IS_CREATE_VARIABLES_DONE:
        if(status.is_create_variables_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;
    
    case PARSE_IS_FILL_VARIABLES_DONE:
        if(status.is_fill_variables_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;
    
    case PARSE_IS_FILL_BLOCKS_DONE:
        if(status.is_fill_blocks_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;    

    default:
        break;
    }

    return EMU_ERR_UNLIKELY;
}