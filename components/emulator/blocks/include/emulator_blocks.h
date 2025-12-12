#pragma once
#include "emulator_types.h"
#include "emulator_interface.h"
#include "emulator_variables.h"
#include "utils_global_access.h"

#define SET_BIT16N(var, n)   ((var) |= (uint16_t)(1U << (n)))
#define _MASK_LEN(n)      ((uint16_t)((1U << (n)) - 1))
#define GET_LOWER_N(val, n)  ((val) & _MASK_LEN(n))
#define CHECK_ALL_N(val, n)  (GET_LOWER_N(val, n) == _MASK_LEN(n))

/**
 * @brief struct handling inputs and outputs one block
*/  
typedef struct _block_handle_s block_handle_t;

/*block specific function pointer*/
typedef emu_err_t (*emu_block_func)(block_handle_t *block);

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



struct _block_handle_s{
    void*           extras;  /*block specific data*/
    emu_block_func block_function;
    uint8_t global_reference_cnt;
    global_acces_t   **global_reference;

    data_types_t*   in_data_type_table; /*array of input datatypes (in order)*/
    void*           in_data;            /*array for all input data*/
    uint8_t*        in_data_offsets;    /*offsets relative to in_data in bytes*/

    data_types_t*   q_data_type_table;  /*array of outputs datatypes (in order)*/
    void*           q_data;             /*array for all output data*/
    uint8_t*        q_data_offsets;     /*offsets relative to q_data in bytes*/

    q_connection_t* q_connections_table; /*reference to all block connections*/

    uint16_t block_idx;          /*id of block (struct)*/
    block_type_t block_type;    /*type of block (function)*/

    uint8_t in_cnt;             /*count of inputs*/
    uint16_t in_set;             /*count of outputs*/

    uint8_t q_cnt;              /*flags (for debug)*/
    uint16_t q_set;              /*flags (for debug)*/

    bool is_executed;           /*has block been executed*/
};








/**
*@brief free all structs and functions from functions table
*/
void block_pass_results(block_handle_t*  block);

/**
 *@brief reset all blokcs 
 */
void emu_blocks_free_all(block_handle_t ** block_structs, uint16_t num_blocks); 


/**
 *@brief Read total count of blocks
 */
emu_err_t emu_parse_total_block_cnt(chr_msg_buffer_t *source);

emu_err_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t ** blocks_list, uint16_t blocks_total_cnt);
