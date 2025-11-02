#pragma once 
#include "stdint.h"
#include "stdbool.h"
#include "gatt_buff.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define GET_4BIT_FIELD(val, idx) (((val) >> ((idx) * 4)) & 0xF)
#define GET_DATA_TYPE(val, idx)  ((data_types_t)GET_4BIT_FIELD(val, idx))

typedef enum {
    EMU_OK = 0,
    EMU_ERR_INVALID_STATE,
    EMU_ERR_INVALID_ARG,
    EMU_ERR_NO_MEMORY,
    EMU_ERR_CMD_START,
    EMU_ERR_CMD_STOP,
    EMU_ERR_CMD_START_BLOCKS,
}emu_err_t;

typedef enum {
    ORD_STOP_BYTES        = 0x0000,
    ORD_START_BYTES       = 0xFFFF,
    ORD_START_BLOCKS      = 0x00FF,
    ORD_PROCESS_VARIABLES = 0x0010,
    ORD_RESET_TRANSMISSION= 0x0001,
    ORD_PROCESS_CODE      = 0x0020,
    ORD_CHECK_CODE        = 0x0030,
    ORD_EMU_RUN           = 0x1000,
    ORD_EMU_STOP          = 0x2000,
} emu_order_code_t;


typedef struct{
    uint16_t id; 
    uint8_t  in_count;   //amount to alocate
    uint64_t in_type;   //type of space to allocate
    uint8_t  q_cnt;
    uint8_t  q_type; 
    uint8_t  q_nodes_count;
}inq_define_t;

typedef struct{
    uint16_t id;
    bool is_executed;  

    uint8_t in_cnt;   //in count 
    uint8_t in_set;   //here store is input set 
    void*   in_data;  //here store copy of data

    uint8_t  q_cnt;        //this is physical count 
    uint8_t  q_nodes_cnt;  //those are nodes (can be more)
    uint16_t*q_ids;        //outs id
    uint8_t  q_set;        //outs paths that have been visited
    void*    q_data;       //there is stored out data
    uint8_t  error_notice;
}inq_handle_t;

emu_err_t emulator_source_assign(chr_msg_buffer_t * msg);
void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt);
inq_handle_t *code_block_init(inq_define_t *inq_params);

void emu(void* params);



