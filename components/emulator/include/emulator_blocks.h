#pragma once
#include "emulator_types.h"
#include "emulator.h"

/*
* this is list of blocks and it's inputs thatat one output is connected to
* lists are size of conn_cnt
*/
typedef struct {
    uint16_t* target_blocks_id_list;  //it's pair of block id and it's input number
    uint8_t*  target_inputs_list;
    uint8_t   conn_cnt;
    uint8_t   in_visited;
} q_connection_t;

typedef struct{
    uint16_t   block_id;
    block_type_t block_type;    
    bool       is_executed;  

    uint8_t  in_cnt;                 //total inputs
    uint8_t  in_set;
    
    data_types_t* in_data_type_table;     //type of variable at each
    void*         in_data;                //common input data pointer
    uint8_t*      in_data_offsets;        //offset of common pointer for each input

    uint8_t  q_cnt; 
    uint8_t  q_set;

    data_types_t*   q_data_type_table;
    void*           q_data;  
    uint8_t*        q_data_offsets;      
    q_connection_t* q_connections_table;  //list of each output connections 

    void*           extras;
   
}block_handle_t;

typedef enum {
    OP_VAR = 0x00,
    OP_ADD = 0x01,
    OP_MUL = 0x02,
    OP_DIV = 0x03,
    OP_SUB = 0x04,
    OP_ROOT = 0x05,
    OP_POWER = 0x06,
    OP_SIN = 0x07,
    OP_COS = 0x08,
    OP_CONST = 0x09,
} op_code_t;

typedef struct {
    op_code_t op;
    uint8_t input_index; //for operator its 00, for constant index in constant table
} instruction_t;

typedef struct{
    instruction_t *code;
    uint8_t count;
    double *constant_table;
} expression_t;

typedef emu_err_t (*emu_block_func)(block_handle_t *block);

emu_err_t block_compute(block_handle_t* block);

void blocks_free_all(block_handle_t** blocks_structs, uint16_t num_blocks); 





