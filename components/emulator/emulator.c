#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "emulator.h"
#include "emulator_body.h"
#include "emulator_blocks.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_parse.h"

static const char * TAG = "EMULATOR TASK";

static chr_msg_buffer_t *source;

/**
* @brief Reset all parse guard flags 
*/
static void _parse_guard_flags_reset(); 

/*Freertos variables*/
QueueHandle_t emu_interface_orders_queue;
parse_guard_flags_t _parse_guard_flags;
emu_mem_t mem;
uint8_t emu_mem_size_single[9];

/*parse checks*/
emu_err_t emu_execute_parse_fill_variables();
emu_err_t emu_execute_parse_allocate_variables();
emu_err_t emu_execute_parse_create_blocks();
emu_err_t emu_execute_parse_fill_blocks();


emu_err_t emu_parse_source_add(chr_msg_buffer_t * msg){
    if (msg == NULL){
        ESP_LOGW(TAG, "no buffer provided");
        return EMU_ERR_INVALID_ARG;   
    }
    source = msg;
    return EMU_OK;
}

emu_err_t emu_execute_loop_start_execution(){
    emu_loop_start();
    return EMU_OK;
}  //start execution

emu_err_t emu_execute_loop_stop_execution(){
    emu_loop_stop();
    return EMU_OK;
}   //stop and preserve state



extern void **blocks_structs;
extern emu_block_func *blocks_fun_table;

void emu_interface_task(void* params){
    ESP_LOGI(TAG, "Emulator interface task created");
    emu_interface_orders_queue  = xQueueCreate(5, sizeof(emu_order_t));
    static emu_order_t orders;
    _parse_guard_flags_reset();
    while(1){
        if (pdTRUE == xQueueReceive(emu_interface_orders_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "execute order 0x%04X" , orders);
            switch (orders){    
                case ORD_PARSE_VARIABLES:
                    emu_execute_parse_allocate_variables();
                    break;
                case ORD_PARSE_INTO_VARIABLES:
                    emu_execute_parse_fill_variables();
                    break;
                case ORD_PROCESS_CODE:
                    // Handle processing code
                    break;

                case ORD_CHECK_CODE:
                    // Handle checking completeness
                    break;
                    
                case ORD_EMU_LOOP_START:
                    emu_loop_start();
                    //add here check if code can be run
                    break;
                case ORD_EMU_LOOP_INIT:
                    if(PARSE_FINISHED()){
                        emu_loop_init();
                    }   
                    else{
                        ESP_LOGE(TAG, "First parse");
                    }
                    break;

                case ORD_EMU_LOOP_STOP:
                    emu_loop_stop();
                    break;

                case ORD_RESET_ALL:
                    chr_msg_buffer_clear(source);
                    if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
                    emu_variables_reset(&mem);}
                    if(PARSE_DONE_FILL_BLOCKS()){
                    emu_execute_blocks_free_all(blocks_structs,5);}
                    _parse_guard_flags_reset();
                    break;
                case ORD_RESET_BLOCKS:
                    emu_execute_blocks_free_all(blocks_structs,5);
                    break;
                case ORD_RESET_MGS_BUF:
                    ESP_LOGI(TAG, "Clearing Msg buffer");   
                    chr_msg_buffer_clear(source);
                    break;

                case ORD_EMU_ALLOCATE_BLOCKS_LIST:
                    emu_create_block_tables(5);
                    emu_execute_parse_create_blocks(source);
                    break;
                case ORD_EMU_FILL_BLOCKS_LIST:
                    emu_execute_parse_fill_blocks(source);

                    break;
                default:
                    // Unknown order
                    break;
            }
        }
    }
};

/*parse checks*/
/*TODO add check errors*/
emu_err_t emu_execute_parse_allocate_variables(){
    if (PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGE(TAG, "Variables parsing already done, or can't be done");
        return EMU_ERR_CMD_START;
    }
    
    ESP_LOGI(TAG, "Parsing variables");
    if(EMU_ERR_NO_MEMORY == emu_parse_variables(source, &mem))
    {   
        ESP_LOGE(TAG, "Parsing variables failed");
        PARSE_SET_ALLOCATE_VAR(false);
        return EMU_ERR_INVALID_STATE;
    }
    PARSE_SET_ALLOCATE_VAR(true);
    PARSE_SET_FILL_VAR(false);
    ESP_LOGI(TAG, "Done parsing variables");
    return EMU_OK;
}
/*parse checks*/
emu_err_t emu_execute_parse_fill_variables(){
    if (!PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGE(TAG, "Cannot fill variables, first allocate space");
        return EMU_ERR_CMD_START;
    }
    //if allocated and filled already
    else if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGW(TAG, "Overwritting variables");
        emu_parse_variables_into(source, &mem);
        ESP_LOGI(TAG, "Filling into done");
        return EMU_OK;
    }
    PARSE_SET_FILL_VAR(true);
    ESP_LOGI(TAG, "Filling variables....");
    emu_parse_variables_into(source, &mem);
    ESP_LOGI(TAG, "Filling done");
    return EMU_OK;
    
}
emu_err_t emu_execute_parse_create_blocks(){
    if(PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGI(TAG, "Creating blocks");
        emu_parse_block(source);
        PARSE_SET_CREATE_BLOCKS(true);
        return EMU_OK;
    }
    else{
        ESP_LOGI(TAG, "Can't create blocks, first create variables");
        return EMU_ERR_CMD_START;
    }
    return EMU_OK;
}

emu_err_t emu_execute_parse_fill_blocks(){
    if(PARSE_DONE_CREATE_BLOCKS()){
        ESP_LOGI(TAG, "Filling blocks");
        PARSE_SET_FILL_BLOCKS(true);
        PARSE_SET_FINISHED(true);
        return EMU_OK;
    }
    else{
        ESP_LOGI(TAG, "Can't fill blocks, first create blocks");
        return EMU_ERR_CMD_START;
    }
    return EMU_OK;
}

/*parse checks*/
static void _parse_guard_flags_reset(){
    PARSE_SET_ALLOCATE_VAR(false);
    PARSE_SET_FILL_VAR(false);
    PARSE_SET_RUN_CODE(false);
    PARSE_SET_CREATE_BLOCKS(false);
    PARSE_SET_FILL_BLOCKS(false);
    PARSE_SET_FINISHED(false);
    return;
}
