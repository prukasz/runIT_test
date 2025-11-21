#include "emulator_blocks.h"
#include "string.h"
#include "emulator_variables.h"
#include <math.h>

extern block_handle_t** blocks_structs;

static inline double get_val_from_block_struct(uint8_t idx, block_handle_t* block) {
    if (!block || idx >= block->in_cnt) {
        return 0.0;
    }
    data_types_t type = block->in_data_type_table[idx];
    void* src = (uint8_t*)block->in_data + block->in_data_offsets[idx];

    double val = 0.0;

    switch (type) {
        case DATA_D:
            memcpy(&val, src, sizeof(double));
            break;
        case DATA_F: {
            float tmp;
            memcpy(&tmp, src, sizeof(float));
            val = (double)tmp;
            break;
        }
        case DATA_I8: {
            int8_t tmp;
            memcpy(&tmp, src, sizeof(int8_t));
            val = (double)tmp;
            break;
        }
        case DATA_I16: {
            int16_t tmp;
            memcpy(&tmp, src, sizeof(int16_t));
            val = (double)tmp;
            break;
        }
        case DATA_I32: {
            int32_t tmp;
            memcpy(&tmp, src, sizeof(int32_t));
            val = (double)tmp;
            break;
        }
        case DATA_UI8: {
            uint8_t tmp;
            memcpy(&tmp, src, sizeof(uint8_t));
            val = (double)tmp;
            break;
        }
        case DATA_UI16: {
            uint16_t tmp;
            memcpy(&tmp, src, sizeof(uint16_t));
            val = (double)tmp;
            break;
        }
        case DATA_UI32: {
            uint32_t tmp;
            memcpy(&tmp, src, sizeof(uint32_t));
            val = (double)tmp;
            break;
        }
        case DATA_B: {
            bool tmp;
            memcpy(&tmp, src, sizeof(bool));
            val = tmp ? 1.0 : 0.0;
            break;
        }
        default:
            val = 0.0;
            break;
    }
    return val;
}
static int cnt = 0;
emu_err_t block_compute(block_handle_t* block){
   
    //ESP_LOGI("compute_block", "executing %d, double %lf", cnt++, get_val_from_block_struct(0, block));
    //ESP_LOGI("compute_block", "executing %d, double %lf", cnt++, get_val_from_block_struct(1, block));
    if(block->q_connections_table[0].in_cnt>0){
        double data = -130.69;
        memcpy(block->q_data, &data, 8);
        //ESP_LOGI("compute_block", "making copy");
        block_fill_results(block);
    }
    return EMU_OK;
}



#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h> // for rounding if needed

static inline void copy_and_cast(void *dst, data_types_t dst_type,
                                 const void *src, data_types_t src_type,
                                 bool round_values)
{
    double val = 0.0;

    // Step 1: read source safely
    switch (src_type) {
        case DATA_UI8:  { uint8_t  tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_UI16: { uint16_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_UI32: { uint32_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_I8:   { int8_t   tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_I16:  { int16_t  tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_I32:  { int32_t  tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_F:    { float    tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
        case DATA_D:    { memcpy(&val, src, sizeof(double)); } break;
        case DATA_B:    { uint8_t  tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp != 0; } break;
        default: return;
    }

    // Optional rounding
    if (round_values) val = round(val);

    // Step 2: write to destination safely
    switch (dst_type) {
        case DATA_UI8:  { if (val < 0) val = 0; if (val > UINT8_MAX) val = UINT8_MAX; *(uint8_t*)dst = (uint8_t)val; } break;
        case DATA_UI16: { if (val < 0) val = 0; if (val > UINT16_MAX) val = UINT16_MAX; *(uint16_t*)dst = (uint16_t)val; } break;
        case DATA_UI32: { if (val < 0) val = 0; if (val > UINT32_MAX) val = UINT32_MAX; *(uint32_t*)dst = (uint32_t)val; } break;
        case DATA_I8:   { if (val < INT8_MIN) val = INT8_MIN; if (val > INT8_MAX) val = INT8_MAX; *(int8_t*)dst = (int8_t)val; } break;
        case DATA_I16:  { if (val < INT16_MIN) val = INT16_MIN; if (val > INT16_MAX) val = INT16_MAX; *(int16_t*)dst = (int16_t)val; } break;
        case DATA_I32:  { if (val < INT32_MIN) val = INT32_MIN; if (val > INT32_MAX) val = INT32_MAX; *(int32_t*)dst = (int32_t)val; } break;
        case DATA_F:    { *(float*)dst = (float)val; } break;
        case DATA_D:    { memcpy(dst, &val, sizeof(double)); } break;
        case DATA_B:    { *(uint8_t*)dst = (val != 0.0) ? 1 : 0; } break;
    }
}

void block_fill_results(block_handle_t* block)
{
    if (!block || !block->q_connections_table) return;

    for (uint8_t current_q = 0; current_q < block->q_cnt; current_q++) {
        for (uint8_t in_idx = 0; in_idx < block->q_connections_table[current_q].in_cnt; in_idx++) {
            block_handle_t* target_block = blocks_structs[block->q_connections_table[current_q].target_block_id[in_idx]];
            if (!target_block) continue;

            uint8_t target_input = block->q_connections_table[current_q].target_in_num[in_idx];
            void* src = (uint8_t*)block->q_data + block->q_data_offsets[current_q];
            void* dst = (uint8_t*)target_block->in_data + target_block->in_data_offsets[target_input];

            copy_and_cast(dst,
                          target_block->in_data_type_table[target_input],
                          src,
                          block->q_data_type_table[current_q], true);
        }
    }
}
