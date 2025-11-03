#include "emulator_loop.h"
#include "emulator_variables.h"
#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "emulator.h"


static chr_msg_buffer_t *source;
static const char * TAG = "EMULATOR TASK";
QueueHandle_t emu_task_q;

static bool is_code_start;
static bool is_code_end;
static bool is_blocks_start;

emu_err_t code_set_start();
emu_err_t code_set_blocks();
emu_err_t code_set_end();
void code_set_reset();

SemaphoreHandle_t emulator_start;
emu_mem_t mem;
emu_data_cnt_t mem_size;


emu_err_t emulator_source_assign(chr_msg_buffer_t * msg){
    if (msg == NULL){
        return EMU_ERR_INVALID_ARG;
    }
    source = msg;
    return EMU_OK;
}

//emu_err_t emulator_source_analyze(){} 


emu_err_t emulator_source_get_varialbes(){
    vTaskDelay(pdMS_TO_TICKS(1000));
    return EMU_OK;
}

//emu_err_t emulator_source_get_config(){}  //get config data from source

emu_err_t loop_start_execution(){
    loop_start();
    return EMU_OK;
}  //start execution

emu_err_t loop_stop_execution(){
    loop_stop();
    return EMU_OK;
}   //stop and preserve state

//emu_err_t emulator_halt_execution(){}   //emergency stop

//emu_err_t emulator_end_execution(){}    //end execution (finish loop)

//emu_err_t emulator_debug_varialbles(){}  //dump values

//emu_err_t emulator_debug_code(){}   //dump code execution state

//emu_err_t emulator_run_with_remote(){}   //allows remote interaction

void loop_task(void* params){
    //form here will be executed all logic 
    while(1){
        uint8_t data =  MEM_GET(DATA_UI8, 1);
        if(pdTRUE == xSemaphoreTake(emulator_start, portMAX_DELAY)){
            ESP_LOGI(TAG, "semaphore taken, %d", data);
            data ++;
            MEM_SET(DATA_UI8, 1, data);
            }
        taskYIELD();
    }
}


inq_handle_t *code_block_init(inq_define_t *inq_params){

    inq_handle_t *handle = (inq_handle_t*)calloc(1, sizeof(inq_handle_t));

    uint16_t size = 0;
    uint8_t bool_cnt = 0;
    for (uint8_t i = 0; i < inq_params->in_count; i++){
    check_size(GET_DATA_TYPE(inq_params->in_type, i),&size, &bool_cnt);
    }//end for
    handle->in_data = calloc(1, size); //allocate memory for copies
    handle->id  = inq_params -> id;

    bool_cnt =0; 
    size = 0;
    for (uint8_t i = 0; i < inq_params->q_cnt; i++){
    check_size(GET_DATA_TYPE(inq_params->q_type, i),&size, &bool_cnt);
    }//end for
        
    handle ->q_data = calloc(1, size); // check data size to allock
    handle ->q_ids = (uint16_t*)calloc(inq_params->q_nodes_count, sizeof(uint16_t));
    free(inq_params);
    return handle;
}

void emu(void* params){
    mem_size.u8 = 10;
    emulator_dataholder_create(&mem, &mem_size);
    mem.u8[1]= 10;
    ESP_LOGI(TAG, "emu task active");
    emu_task_q  = xQueueCreate(3, sizeof(emu_order_code_t));
    static emu_order_code_t orders;
    while(1){
        if (pdTRUE == xQueueReceive(emu_task_q, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "execute order 0x%04X" , orders);
            switch (orders){
            case ORD_PROCESS_VARIABLES:
                // Handle processing variables
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
                // Handle reset
                code_set_reset();
                chr_msg_buffer_clear(source);
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

