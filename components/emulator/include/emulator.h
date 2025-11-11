#pragma once 
#include "stdint.h"
#include "gatt_buff.h"
#include "emulator_types.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHECK_PTR(ptr, name_str) ({                                  \
    if ((ptr) == NULL) {                                             \
        ESP_LOGE(TAG, "Null pointer: %s (function: %s)",             \
                 (name_str), __func__);                              \
        return EMU_ERR_NO_MEMORY;                                    \
    }                                                                \
})


/*max 16 inputs (apart from global variables)*/
typedef struct {
    uint16_t *block_id;   // ID of the connected block
    uint8_t  q_number;  // Which output of this block connects
    uint16_t blocks_visited;
} q_connection_t;

typedef struct{
    uint16_t id;
    block_id_t type;
    bool is_executed;  

    uint8_t  in_cnt;                 //input counts 
    uint16_t in_set;                 //here store input 

    data_types_t *in_data_type_table;//type of variable in each input 
    void*    in_data;                 //here store copy of data
    uint8_t * in_data_offsets;        //max 16 inputs 

    uint8_t  q_cnt; 
    uint8_t  q_set;

    data_types_t *q_data_type_table;
    void*     q_data;  
    uint8_t*  q_data_offsets;         //max 16 outputs
    
    q_connection_t* q_connections_id; //connected block with id and number of out connceted to 
                    
      //visited blocks count
                    
    uint8_t  error_notice;        
}block_handle_t;

emu_err_t emulator_parse_source_add(chr_msg_buffer_t * msg);

block_handle_t *block_init();


void emu(void* params);
