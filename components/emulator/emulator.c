#include "emulator_loop.h"
#include "emulator_variables.h"
#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "emulator.h"


static chr_msg_buffer_t source;
static const char * MAIN_BODY = "EMULATOR TASK";
static uint32_t stack_depth = 10*1024;
static UBaseType_t task_priority = 4;
static TaskHandle_t emulator_body_handle;


SemaphoreHandle_t emulator_start;
emu_mem_t mem;
emu_size_t mem_size;


emulator_err_t emulator_init(){
    mem_size.d = 100;
    mem_size.i8 = 100;
    emulator_dataholder_create(&mem, &mem_size);
    emulator_start = xSemaphoreCreateBinary();
    xTaskCreate(emulator_body_task, MAIN_BODY, stack_depth, NULL, task_priority, &emulator_body_handle);
    return EMU_OK;
}
emulator_err_t emulator_source_assign(chr_msg_buffer_t * msg){
    if (msg == NULL){
        return EMU_ERR_INVALID_ARG;
    }
    source = *msg;
    return EMU_OK;
}

//emulator_err_t emulator_source_analyze(){}   //check packets completeness

//emulator_err_t emulator_source_get_varialbes(){}  //parse variable list

//emulator_err_t emulator_source_get_config(){}  //get config data from source

emulator_err_t emulator_start_execution(){
    loop_create_set_period(100000); ///temporary
    loop_start();
    return EMU_OK;
}  //start execution

emulator_err_t emulator_stop_execution(){
    loop_stop();
    return EMU_OK;
}   //stop and preserve state

//emulator_err_t emulator_halt_execution(){}   //emergency stop

//emulator_err_t emulator_end_execution(){}    //end execution (finish loop)

//emulator_err_t emulator_debug_varialbles(){}  //dump values

//emulator_err_t emulator_debug_code(){}   //dump code execution state

//emulator_err_t emulator_run_with_remote(){}   //allows remote interaction

void emulator_body_task(void* params){
    //form here will be executed all logic 
    while(1){
        if(pdTRUE == xSemaphoreTake(emulator_start, portMAX_DELAY)){
            ESP_LOGI("emu", "semaphore taken");
        }
        taskYIELD();
    }
}


inq_handle_t *code_init(inq_define_t *inq_params){

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

void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt){
      
      switch ((data_types_t)x) {
            case DATA_UI8:
            case DATA_I8:   
                (*total) += sizeof(uint8_t);
                break;
            case DATA_UI16:
            case DATA_I16:
                (*total) += sizeof(uint16_t);
                break;
            case DATA_UI32:
            case DATA_I32:
            case DATA_F:
                (*total) += sizeof(uint32_t);
                break;
            case DATA_UI64:
            case DATA_I64:
            case DATA_D:
                (*total) += sizeof(uint64_t);
                break;
            case DATA_B:
                (*bool_cnt) ++;
            default:
                break;
            (*total) += (*bool_cnt+ 7) / 8;
        }
}






