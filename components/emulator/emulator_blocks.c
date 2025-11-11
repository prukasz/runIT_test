#include "emulator_blocks.h"
#include "string.h"
#include "emulator_variables.h"

extern block_handle_t** blocks_structs=NULL;

emu_err_t block_compute(block_handle_t* block){
    ESP_LOGI("compute_block", "executing");
    return EMU_OK;
}

void block_fill_results(block_handle_t* block){
    for(uint8_t q = 0; q<block->q_cnt; q++){
        for(uint8_t in = 0; in < block->q_connections_id[q].in_cnt; in++){
            block_handle_t* block_to_fill = block->q_connections_id->block_id[in];
            uint8_t input_to_fill = block->q_connections_id->in_num[in];
            size_t size = data_size(block_to_fill->in_data_type_table[input_to_fill]);
            memcpy((uint8_t*)block_to_fill->in_data+block_to_fill->in_data_offsets[in],&block->q_data[block->q_data_offsets[q]], size);
        }
    }
}
