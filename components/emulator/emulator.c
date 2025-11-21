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


block_handle_t *block_create_struct(uint8_t in_cnt, uint8_t q_cnt){
    block_handle_t *block = calloc(1, sizeof(block_handle_t));
    block -> in_cnt = in_cnt;
    block -> q_cnt  = q_cnt;
    block -> in_data_offsets = calloc((size_t)in_cnt, sizeof(uint16_t));
    block -> q_data_offsets  = calloc((size_t)q_cnt, sizeof(uint16_t));
    block->in_data_type_table = calloc((size_t)in_cnt, sizeof(data_types_t));
    block->q_data_type_table  = calloc((size_t)q_cnt, sizeof(data_types_t));
    block->q_connections_table   = calloc((size_t)q_cnt, sizeof(q_connection_t));
    return block;
}
extern block_handle_t** blocks_structs;
extern emu_block_func *blocks_table;
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

                case ORD_EMU_CREATE_BLOCK_STRUCT:
                    emu_create_block_tables(2);
                    blocks_structs[0] = block_create_struct(2, 2);
        blocks_structs[1] = block_create_struct(2, 2);

        // -------------------------
        // BLOCK 0
        // -------------------------

        // Input types
        blocks_structs[0]->in_data_type_table[0] = DATA_D;
        blocks_structs[0]->in_data_type_table[1] = DATA_D;

        // Input offsets (in bytes!)
        blocks_structs[0]->in_data_offsets[0] = 0;
        blocks_structs[0]->in_data_offsets[1] = 8;

        // Allocate input memory (2 doubles → 16 bytes)
        blocks_structs[0]->in_data = calloc(1, 2 * sizeof(double));

        // Output types
        blocks_structs[0]->q_data_type_table[0] = DATA_D;
        blocks_structs[0]->q_data_type_table[1] = DATA_D;

        // Output offsets
        blocks_structs[0]->q_data_offsets[0] = 0;
        blocks_structs[0]->q_data_offsets[1] = 8;

        // Allocate q memory (2 doubles → 16 bytes)
        blocks_structs[0]->q_data = calloc(1, 2 * sizeof(double));


        // -------------------------
        // BLOCK 1
        // -------------------------

        // Input types
        blocks_structs[1]->in_data_type_table[0] = DATA_I8;
        blocks_structs[1]->in_data_type_table[1] = DATA_I8;

        // Input offsets
        blocks_structs[1]->in_data_offsets[0] = 0;
        blocks_structs[1]->in_data_offsets[1] = 8;

        // Allocate input memory
        blocks_structs[1]->in_data = calloc(1, 2 * sizeof(double));

        // Output types
        blocks_structs[1]->q_data_type_table[0] = DATA_D;
        blocks_structs[1]->q_data_type_table[1] = DATA_D;

        // Output offsets
        blocks_structs[1]->q_data_offsets[0] = 0;
        blocks_structs[1]->q_data_offsets[1] = 8;

        // Allocate q memory
        blocks_structs[1]->q_data = calloc(1, 2 * sizeof(double));


        // -------------------------
        // CONNECTION: block 0 → block 1
        // q0 of block 0 feeds in0 of block 1
        // -------------------------

        blocks_structs[0]->q_connections_table = calloc(2, sizeof(q_connection_t));

        // One target for q0
        blocks_structs[0]->q_connections_table[0].in_cnt = 1;

        // Allocate target block id list
        blocks_structs[0]->q_connections_table[0].target_block_id =
            calloc(1, sizeof(uint16_t));
        blocks_structs[0]->q_connections_table[0].target_block_id[0] = 1;

        // target input index list
        blocks_structs[0]->q_connections_table[0].target_in_num =
            calloc(1, sizeof(uint8_t));
        blocks_structs[0]->q_connections_table[0].target_in_num[0] = 1;
                    blocks_table[0] = block_compute;
                    blocks_table[1] = block_compute;
                    
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
