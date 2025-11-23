#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_parse.h"
#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "emulator.h"
#include "string.h"
#include "emulator_body.h"
#include "emulator_blocks.h"

static const char * TAG = "EMULATOR TASK";

static chr_msg_buffer_t *source;

/*Freertos variables*/
QueueHandle_t emu_task_queue;
parse_status_t parse_status;

emu_mem_t mem;
uint8_t emu_mem_size_single[9];

/*parse checks*/
emu_err_t parse_fill_variables();
emu_err_t parse_allocate_variables();
emu_err_t parse_create_blocks();
emu_err_t parse_fill_blocks();
void parse_reset(); 

emu_err_t emulator_parse_source_add(chr_msg_buffer_t * msg){
    if (msg == NULL){
        ESP_LOGW(TAG, "no buffer provided");
        return EMU_ERR_INVALID_ARG;   
    }
    source = msg;
    return EMU_OK;
}

emu_err_t loop_start_execution(){
    loop_start();
    return EMU_OK;
}  //start execution

emu_err_t loop_stop_execution(){
    loop_stop();
    return EMU_OK;
}   //stop and preserve state



extern block_handle_t** blocks_structs;
extern emu_block_func *blocks_fun_table;
void emu(void* params){
    ESP_LOGI(TAG, "emu task active");
    emu_task_queue  = xQueueCreate(3, sizeof(emu_order_t));
    static emu_order_t orders;
    parse_reset();
    while(1){
        if (pdTRUE == xQueueReceive(emu_task_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "execute order 0x%04X" , orders);
            switch (orders){    
                case ORD_PROCESS_VARIABLES:
                    parse_allocate_variables();
                    break;
                case ORD_FILL_VARIABLES:
                    parse_fill_variables();
                    break;
                case ORD_PROCESS_CODE:
                    // Handle processing code
                    break;

                case ORD_CHECK_CODE:
                    // Handle checking completeness
                    break;
                    
                case ORD_EMU_LOOP_RUN:
                    loop_start();
                    //add here check if code can be run
                    break;
                case ORD_EMU_LOOP_INIT:
                    if(PARSE_FINISHED()){
                        loop_init();
                    }   
                    else{
                        ESP_LOGE(TAG, "First parse");
                    }
                    break;

                case ORD_EMU_LOOP_STOP:
                    loop_stop();
                    break;

                case ORD_RESET_ALL:
                    chr_msg_buffer_clear(source);
                    if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
                    emu_variables_reset(&mem);}
                    if(PARSE_DONE_FILL_BLOCKS()){
                    blocks_free_all(blocks_structs,5);}
                    parse_reset();
                    break;
                case ORD_RESET_BLOCKS:
                    blocks_free_all(blocks_structs,5);
                    break;
                case ORD_RESET_MGS_BUF:
                    ESP_LOGI(TAG, "Clearing Msg buffer");   
                    chr_msg_buffer_clear(source);
                    break;

                case ORD_EMU_CREATE_BLOCK_STRUCT:
                    emu_create_block_tables(5);
                    parse_create_blocks(source);
                    break;
                case ORD_EMU_FILL_BLOCK_STRUCT:
                    parse_fill_blocks(source);

                    break;
                default:
                    // Unknown order
                    break;
            }// end case
        }//end if
    } //end while
};

/*parse checks*/
/*TODO add and check errors*/
emu_err_t parse_allocate_variables(){
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
emu_err_t parse_fill_variables(){
    if (!PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGE(TAG, "Cannot fill variables, first allocate space");
        return EMU_ERR_CMD_START;
    }
    //if allocated and filled already
    else if(PARSE_DONE_FILL_VAR()&&PARSE_DONE_ALLOCATE_VAR()){
        ESP_LOGW(TAG, "Overwritting variables");
        emu_parse_into_variables(source, &mem);
        ESP_LOGI(TAG, "Filling into done");
        return EMU_OK;
    }
    PARSE_SET_FILL_VAR(true);
    ESP_LOGI(TAG, "Filling variables....");
    emu_parse_into_variables(source, &mem);
    ESP_LOGI(TAG, "Filling done");
    return EMU_OK;
    
}
emu_err_t parse_create_blocks(){
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

emu_err_t parse_fill_blocks(){
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
void parse_reset(){
    PARSE_SET_ALLOCATE_VAR(false);
    PARSE_SET_FILL_VAR(false);
    PARSE_SET_RUN_CODE(false);
    PARSE_SET_CREATE_BLOCKS(false);
    PARSE_SET_FILL_BLOCKS(false);
    PARSE_SET_FINISHED(false);
    return;
}
