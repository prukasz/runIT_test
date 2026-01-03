#pragma once
#include "emulator_types.h"
#include "emulator_interface.h"
#include "emulator_variables.h"

typedef struct{
    __attribute((packed))struct{
        uint16_t block_idx;
        uint8_t block_type;  
        uint16_t in_connected;
        uint8_t in_cnt;      //total inputs count
        uint8_t q_cnt;       //total outputs count
    }cfg;
    void *custom_data; //block specific instructions
    void **inputs;  // mem_acces_s_t | mem_acces_arr_t
    emu_mem_instance_iter_t *outputs; // emu_mem_instance_s_t | emu_mem_instance_arr_t
}block_handle_t;


typedef emu_result_t (*emu_block_func)(block_handle_t *block);
typedef emu_result_t (*emu_block_parse_func)(chr_msg_buffer_t *source, block_handle_t *block);
typedef void (*emu_block_free_func)(block_handle_t *block);



emu_result_t emu_block_set_output(block_handle_t *block, emu_variable_t *var, uint8_t num);
bool emu_block_check_inputs_updated(block_handle_t *block);
void emu_block_reset_outputs_status(block_handle_t *block);

/**
*@brief free all structs
*/
void emu_blocks_free_all(block_handle_t **block_structs, uint16_t num_blocks); 

/**
*@brief free selected block
*/
void emu_block_free(block_handle_t *block);

/**
 *@brief Read total count of blocks
 */
emu_result_t emu_parse_total_block_cnt(chr_msg_buffer_t *source, block_handle_t ***blocks_list, uint16_t *blocks_total_cnt);
/**
 *@brief Parse and create block
 */
emu_result_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t ** blocks_list, uint16_t blocks_total_cnt);
