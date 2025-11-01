#pragma once 
#include "stdint.h"
#include "emulator_loop.h"

#define GET_4BIT_FIELD(val, idx) (((val) >> ((idx) * 4)) & 0xF)
#define GET_DATA_TYPE(val, idx)  ((data_types_t)GET_4BIT_FIELD(val, idx))

typedef enum{
    DATA_UI8,
    DATA_UI16,
    DATA_UI32,
    DATA_UI64,
    DATA_I8,
    DATA_I16,
    DATA_I32,
    DATA_I64,
    DATA_F,
    DATA_D,
    DATA_B
}data_types_t;

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
    uint8_t error_notice;
}inq_handle_t;



void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt);
inq_handle_t *code_init(inq_define_t *inq_params);




