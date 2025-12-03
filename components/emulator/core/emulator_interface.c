#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emulator_interface.h"
#include "emulator_body.h"
#include "emulator_blocks.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
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

static void _interface_parse_guard_flags_reset(); 

/**********************************************************************/
/**
 * @brief This queue is used in ble callback to give orders
 */
QueueHandle_t emu_interface_orders_queue;
/**********************************************************************/
parse_guard_flags_t _parse_guard_flags;

/***************************************************************************/
/*             Functions that are executed when order comes in             */

static emu_err_t _interface_execute_parse_fill_variables(void);
static emu_err_t _interface_execute_parse_allocate_variables(void);
static emu_err_t _interface_execute_parse_allocate_blocks(void);
static emu_err_t _interface_execute_parse_fill_blocks(void);
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
    _interface_parse_guard_flags_reset();
    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "Received order: 0x%04X" , orders);
            switch (orders){    
                case ORD_PARSE_VARIABLES:
                    _interface_execute_parse_allocate_variables();
                    ESP_LOGI(TAG, "Free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                    break;
                case ORD_PARSE_INTO_VARIABLES:
                    _interface_execute_parse_fill_variables();
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
                    if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
                        emu_variables_reset(&mem);
                    }
                    if(PARSE_DONE_FILL_BLOCKS()){
                        //temporary
                        emu_execute_blocks_free_all(emu_global_blocks_structs,5);
                    }
                    _interface_parse_guard_flags_reset();
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
                    _interface_execute_parse_allocate_blocks();
                    ESP_LOGI(TAG, "Free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                    break;
                case ORD_EMU_FILL_BLOCKS_LIST:
                    _interface_execute_parse_fill_blocks();

                    break;
                default:
                    // Unknown order
                    break;
            }
        }
    }
};

/**
*@brief Execute order from user: create provided variables (allocate space)
*/
static emu_err_t _interface_execute_parse_allocate_variables(void){
    if (PARSE_DONE_ALLOCATE_VAR()){//check if already done
        ESP_LOGE(TAG, "Variables parsing already done, or can't be done");
        return EMU_ERR_ORD_CANNOT_EXECUTE;  
    }
    emu_err_t err = emu_parse_variables(source, &mem);
    if(EMU_ERR_NO_MEMORY == err)
    {   
        PARSE_SET_ALLOCATE_VAR(false); 
        return EMU_ERR_NO_MEMORY;
    }else if (EMU_OK!=err)
    {
        ESP_LOGE(TAG, "While parsing variables error: %d", err);
        PARSE_SET_ALLOCATE_VAR(false); 
        return err;
    }else{
        PARSE_SET_ALLOCATE_VAR(true);  //set that allocating already done 
        PARSE_SET_FILL_VAR(false);     //set that now can parse into variables
        ESP_LOGI(TAG, "Done parsing variables");
        return EMU_OK;
    }
    return EMU_ERR_UNLIKELY;
}

/**
*@brief Execute order from user: fill created variables with provided data
*/
static emu_err_t _interface_execute_parse_fill_variables(void){
    if (!PARSE_DONE_ALLOCATE_VAR()){  //check if space aviable
        ESP_LOGE(TAG, "Cannot fill variables, first allocate space");
        return EMU_ERR_ORD_CANNOT_EXECUTE;
    }
    //check if parsing already done
    else if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGW(TAG, "Re-filling variables");
        emu_err_t err = emu_parse_variables_into(source, &mem);
        if(EMU_OK!=err){
            ESP_LOGE(TAG, "While re-parsing into variables error: %d", err);
            return err;
        }else{
            PARSE_SET_FILL_VAR(true);
            ESP_LOGI(TAG, "Re-filling into variables done");
            return EMU_OK; 
        }  
    }
    ESP_LOGI(TAG, "Filling into variables");
    emu_err_t err = emu_parse_variables_into(source, &mem);
    if(EMU_OK!=err){
            ESP_LOGE(TAG, "While parsing into variables error: %d", err);
            return err;
        }else{
            PARSE_SET_FILL_VAR(true);
            ESP_LOGI(TAG, "Filling into done");
            return EMU_OK; 
        }
    return EMU_ERR_UNLIKELY;
}

/**
*@brief Execute order from user: create and populate struct for blocks
*/
static emu_err_t _interface_execute_parse_allocate_blocks(void){
    //check if variables already created
    if(PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGI(TAG, "Creating blocks");
        emu_err_t err= emu_parse_block(source);
        if(EMU_OK!=err){
            ESP_LOGE(TAG, "While allocating blocks error: %d", err);
            return err;
        }else{
            PARSE_SET_CREATE_BLOCKS(true);
            ESP_LOGI(TAG, "Filling into done");
            return EMU_OK; 
        }
    }else{
        ESP_LOGI(TAG, "Can't create blocks, first create variables");
        return EMU_ERR_ORD_CANNOT_EXECUTE;
    }
    return EMU_ERR_UNLIKELY;
}

/**
*@brief Execute order from user: fill block struct with custom data
*/
static emu_err_t _interface_execute_parse_fill_blocks(void){
    if(PARSE_DONE_CREATE_BLOCKS()){
        ESP_LOGI(TAG, "Filling blocks");
        PARSE_SET_FILL_BLOCKS(true);
        //dummy code
        PARSE_SET_FINISHED(true);
        return EMU_OK;
    }
    else{
        ESP_LOGI(TAG, "Can't fill blocks, first create blocks");
        return EMU_ERR_ORD_CANNOT_EXECUTE;
    }
    return EMU_OK;
}

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
    if(PARSE_FINISHED()){
        emu_err_t err = emu_loop_init();
        if (EMU_OK!= err){
            ESP_LOGE(TAG, "While creating loop error: %d", err);
            return err;
        }
    }else{
        ESP_LOGE(TAG, "Cannot init loop, first parse code");
        return EMU_ERR_ORD_CANNOT_EXECUTE;
    }
    return EMU_OK;
}   



/**
* @brief Reset flags on parse comleteness status
*/
static void _interface_parse_guard_flags_reset(){
    PARSE_SET_ALLOCATE_VAR(false);
    PARSE_SET_FILL_VAR(false);
    PARSE_SET_RUN_CODE(false);
    PARSE_SET_CREATE_BLOCKS(false);
    PARSE_SET_FILL_BLOCKS(false);
    PARSE_SET_FINISHED(false);
    return;
}

emu_err_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        return EMU_ERR_NULL_POINTER;   
    }
    source = msg;
    return EMU_OK;
}
