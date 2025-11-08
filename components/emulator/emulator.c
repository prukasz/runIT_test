#include "emulator_loop.h"
#include "emulator_variables.h"
#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

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

//emulator_err_t emulator_source_get_variables(){}  //parse variable list

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








