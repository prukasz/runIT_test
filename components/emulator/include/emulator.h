#pragma once 
#include "stdint.h"
#include "stdbool.h"
#include "gatt_buff.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "emulator_types.h"

/**
* @brief commands from BLE
*/

typedef struct{
    uint16_t id;
    block_id_t type;
    uint8_t in_cnt;
    data_types_t *in_data_type_table;
    uint8_t  q_cnt;
    uint8_t  q_nodes_cnt;
    uint16_t*q_node_ids;
    data_types_t *q_data_type_table;
}block_define_t;

typedef struct{
    uint16_t id;
    block_id_t type;
    bool is_executed;  

    uint8_t  in_cnt;                 //input counts 
    uint64_t in_set;                 //here store input 
    data_types_t *in_data_type_table;//type of variable in each input 
    void*   in_data;                 //here store copy of data
    size_t * in_data_offsets;

    uint8_t  q_cnt;                  //out count
    uint8_t  q_nodes_cnt;            //blocks connected to output
    uint16_t*q_node_ids;                  //if of blocks connected to output
    uint8_t  q_set;                 
    data_types_t *q_data_type_table; //visited blocks count
    void*    q_data;                 //output data is stored here
    uint8_t  error_notice;
    size_t * q_data_offsets;          
}block_handle_t;

emu_err_t emulator_parse_source_add(chr_msg_buffer_t * msg);
block_handle_t *code_block_init(block_define_t *inq_params);

void emu(void* params);
