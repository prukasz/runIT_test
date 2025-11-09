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

static const char * TAG = "EMULATOR TASK";

static chr_msg_buffer_t *source;

/*Freertos variables*/
QueueHandle_t emu_task_queue;
parse_status_t parse_status;

emu_mem_t mem;
uint8_t emu_mem_size_single[9];

emu_err_t parse_fill_variables();
emu_err_t parse_allocate_variables();
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

block_handle_t *block_init(block_define_t *block_define){
    //init block
    block_handle_t *block = (block_handle_t*)calloc(1,sizeof(block_handle_t));
    block->id = block_define->id; 
    block->type = block_define->type;
    block->in_cnt = block_define->in_cnt;
    block->in_data_type_table = (data_types_t*)calloc((block->in_cnt),sizeof(data_types_t));
    memcpy(block->in_data_type_table, block_define->in_data_type_table, block->in_cnt*sizeof(data_types_t));
    block->q_cnt = block_define->q_cnt;
    block->q_nodes_cnt = block_define->q_nodes_cnt;
    block->q_node_ids = (uint16_t*)calloc(block->q_nodes_cnt, sizeof(uint16_t));
    memcpy(block->q_node_ids, block_define->q_node_ids, block->q_nodes_cnt*sizeof(uint16_t));
    block->q_data_type_table = (data_types_t*)calloc((block->q_cnt),sizeof(data_types_t));
    free(block_define);
    
    
    block->in_data_offsets=(size_t*)calloc(block->in_cnt,sizeof(size_t));
    size_t size_to_allocate = 0;
    //allocating space for left pointers
    for(int i = 0; i<block->in_cnt; i++)
    {
        block->in_data_offsets[i] = size_to_allocate;
        size_to_allocate = size_to_allocate + data_size(block->in_data_type_table[i]);
    }

    block->in_data = (void*)calloc(1,size_to_allocate);

    block->q_data_offsets=(size_t*)calloc(block->q_cnt,sizeof(size_t));
    size_to_allocate = 0;
    for(int i = 0; i<block->q_cnt; i++)
    {
        block->q_data_offsets[i] = size_to_allocate;
        size_to_allocate = size_to_allocate + data_size(block->q_data_type_table[i]);
    }

    block->q_data = (void*)calloc(1,size_to_allocate);

    return block;
}

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
                    if(!PARSE_CAN_ALLOCATE()){
                    loop_init();
                    }
                    else{
                        ESP_LOGE(TAG, "First allocate variables");
                    }
                break;

                case ORD_EMU_LOOP_STOP:
                    loop_stop();
                    break;

                case ORD_RESET_TRANSMISSION:
                    parse_reset();
                    chr_msg_buffer_clear(source);
                    emu_variables_reset(&mem);
                    break;

                default:
                    // Unknown order
                    break;
            }// end case
        }//end if
    } //end while
};


/*parse checks*/
emu_err_t parse_allocate_variables(){
    if (!PARSE_CAN_ALLOCATE()){
        ESP_LOGE(TAG, "Variables parsing already done");
        return EMU_ERR_CMD_START;
    }
    
    ESP_LOGI(TAG, "Parsing variables");
    if(EMU_ERR_NO_MEMORY == emu_parse_variables(source, &mem))
    {   
        ESP_LOGE(TAG, "Parsing variables failed");
        PARSE_SET_ALLOCATE(true);
        return EMU_ERR_INVALID_STATE;
    }
    PARSE_SET_ALLOCATE(false);
    ESP_LOGI(TAG, "Done parsing variables");
    return EMU_OK;
}
/*parse checks*/
emu_err_t parse_fill_variables(){
    if (PARSE_CAN_ALLOCATE()){
        ESP_LOGE(TAG, "Cannot fill variables, first allocate space");
        return EMU_ERR_CMD_START;
    }
    else if(!PARSE_CAN_FILL_VAL()){
        ESP_LOGW(TAG, "Overwritting variables");
        emu_parse_into_variables(source, &mem);
        ESP_LOGI(TAG, "Filling into done");
        return EMU_OK;
    }
    PARSE_SET_FILL_VAL(false);
    ESP_LOGI(TAG, "Filling into variables");
    emu_parse_into_variables(source, &mem);
    ESP_LOGI(TAG, "Filling into done");
    return EMU_OK;
}
/*parse checks*/
void parse_reset(){
    PARSE_SET_ALLOCATE(true);
    PARSE_SET_FILL_VAL(true);
    PARSE_SET_RUN_CODE(false);
    return;
}
