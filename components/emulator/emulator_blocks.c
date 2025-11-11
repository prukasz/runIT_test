#include "emulator_blocks.h"
#include "string.h"
#include "emulator_variables.h"

extern block_handle_t** blocks_structs;

emu_err_t block_compute(block_handle_t* block){
    ESP_LOGI("compute_block", "executing");


    
    if(!block->q_connections_id==NULL){
    block_fill_results(block);
    }
    return EMU_OK;
}


void copy_and_cast(void* dst, data_types_t dst_type, void* src, size_t src_size) {
    switch (dst_type) {
        case DATA_UI8:   *(uint8_t*)dst  = *(uint8_t*)src; break;
        case DATA_UI16:  *(uint16_t*)dst = *(uint16_t*)src; break;
        case DATA_UI32:  *(uint32_t*)dst = *(uint32_t*)src; break;
        case DATA_I8:    *(int8_t*)dst   = *(int8_t*)src; break;
        case DATA_I16:   *(int16_t*)dst  = *(int16_t*)src; break;
        case DATA_I32:   *(int32_t*)dst  = *(int32_t*)src; break;
        case DATA_F:   *(float*)dst    = *(float*)src; break;
        case DATA_D:  *(double*)dst   = *(double*)src; break;
        case DATA_B:    *(bool*)dst     = (*(uint8_t*)src != 0);
        break;
    }
}

void block_fill_results(block_handle_t* block) {
    for (uint8_t q = 0; q < block->q_cnt; q++) {
        for (uint8_t in_idx = 0; in_idx < block->q_connections_id[q].in_cnt; in_idx++) {
            // Target block and input index
            block_handle_t* target_block = blocks_structs[block->q_connections_id[q].block_id[in_idx]];
            uint8_t target_input = block->q_connections_id[q].in_num[in_idx];

            // Pointers to source (q_data) and destination (in_data)
            void* src = (uint8_t*)block->q_data + block->q_data_offsets[q];
            void* dst = (uint8_t*)target_block->in_data + target_block->in_data_offsets[target_input];
            
            // Use the helper to copy and cast
            copy_and_cast(dst, target_block->in_data_type_table[target_input],
                          src, data_size(block->q_data_type_table[q]));
        }
    }
}

