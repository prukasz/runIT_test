#pragma once
#include "emulator_types.h"
#include "emulator.h"
#include "emulator_variables.h"

/*block specific function pointer*/
typedef emu_err_t (*emu_block_func)(void *block);

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


/**
 * @brief struct handling inputs and outputs of each block
*/
typedef struct {
    void*           extras;  /*block specific data*/
    _global_val_acces_t **global_reference;

    data_types_t*   in_data_type_table; /*array of input datatypes (in order)*/
    void*           in_data;            /*array for all input data*/
    uint8_t*        in_data_offsets;    /*offsets relative to in_data in bytes*/

    data_types_t*   q_data_type_table;  /*array of outputs datatypes (in order)*/
    void*           q_data;             /*array for all output data*/
    uint8_t*        q_data_offsets;     /*offsets relative to q_data in bytes*/

    q_connection_t* q_connections_table; /*reference to all block connections*/

    uint16_t block_id;          /*id of block (struct)*/
    block_type_t block_type;    /*type of block (function)*/

    uint8_t in_cnt;             /*count of inputs*/
    uint8_t in_set;             /*count of outputs*/

    uint8_t q_cnt;              /*flags (for debug)*/
    uint8_t q_set;              /*flags (for debug)*/

    bool is_executed;           /*has block been executed*/
} block_handle_t;


/**
*@brief compute block - handling math operations
*/
emu_err_t block_compute(void* src);


/**
*@brief free all structs and functions from functions table
*/
void emu_execute_blocks_free_all(void** blocks_structs, uint16_t num_blocks); 







