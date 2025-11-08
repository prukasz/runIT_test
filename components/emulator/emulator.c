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
QueueHandle_t emu_task_queue;

static bool is_code_start;
static bool is_code_end;
static bool is_blocks_start;

emu_err_t code_set_start();
emu_err_t code_set_blocks();
emu_err_t code_set_end();
void code_set_reset();

extern SemaphoreHandle_t loop_semaphore;
extern SemaphoreHandle_t wtd_semaphore;

emu_mem_t mem;

uint8_t emu_mem_size_single[9];

extern emu_wtd_t wtd;

#define TABLE_SIZE 3
#define ITERATIONS 500000
/*main block logic*/
void loop_task(void* params){
    while(1){
        if(pdTRUE == xSemaphoreTake(loop_semaphore, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            /*
            for(int i = 0; i < TABLE_SIZE; i++){
                for(int j = 0; j < TABLE_SIZE; j++){
                    MEM_GET_U8(0, i, j, SIZE_MAX);
                    ESP_LOGI(TAG, "table i %d j %d vaj %d", i ,j ,MEM_GET_U8(0, i, j, SIZE_MAX));
                }
            }
            HERE
            LOOP
            LOGIC
            EXECUTION
            */
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %.3f ms, watchdog triggered: %d", (end_time - start_time) / 1000.0, wtd.wtd_triggered);
            xSemaphoreGive(wtd_semaphore);
            taskYIELD();
        }
    }
}

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
    while(1){
        if (pdTRUE == xQueueReceive(emu_task_queue, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "execute order 0x%04X" , orders);
            switch (orders){
            case ORD_PROCESS_VARIABLES:
                emu_parse_variables(source, &mem);
                break;

            case ORD_PROCESS_CODE:
                // Handle processing code
                break;

            case ORD_CHECK_CODE:
                // Handle checking completeness
                break;

            case ORD_START_BLOCKS:
                // Handle starting blocks
                code_set_blocks();
                break;

            case ORD_EMU_LOOP_RUN:
                loop_start();
                break;
            case ORD_EMU_LOOP_INIT:
                loop_init();
            break;

            case ORD_EMU_LOOP_STOP:
                loop_stop();
                break;

            case ORD_RESET_TRANSMISSION:
                code_set_reset();
                chr_msg_buffer_clear(source);
                emu_variables_reset(&mem);
                break;
            case ORD_START_BYTES:
                // Handle start bytes
                code_set_start();
                break;

            case ORD_STOP_BYTES:
                code_set_end();
                break;

            default:
                // Unknown order
                break;
            }// end case
        }//end if
    } //end while
};



//flags for transmission keypoints
emu_err_t code_set_start(){
    if (is_code_start == true){
        ESP_LOGE(TAG, "code transmission already started");
        return EMU_ERR_CMD_START;
    }
    is_code_start = true;
    ESP_LOGI(TAG, "code transmission started");
    return EMU_OK;
}
emu_err_t code_set_blocks(){
    if (is_blocks_start == true){
        ESP_LOGE(TAG, "blocks transmission already started");
        return EMU_ERR_CMD_START;
    }
    is_blocks_start = true;
    ESP_LOGI(TAG, "blocks transmission started");
    return EMU_OK;
}
emu_err_t code_set_end(){
    if (is_code_end == true){
        ESP_LOGE(TAG, "code already stopped");
        return EMU_ERR_CMD_START;
    }
    is_code_end = true;
    ESP_LOGI(TAG, "code end reached");
    return EMU_OK;
}

void code_set_reset(){
    is_code_start   = false;
    is_blocks_start = false;
    is_code_end     = false;
    return;
}
