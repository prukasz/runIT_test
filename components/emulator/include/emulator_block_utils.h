#pragma once 
#include "emulator_blocks.h"
#include "math.h"
#include "string.h"
/*
*common functions that can be used in blocks
*/



/*
* @brief this return value of selected input in double format 
*/
static inline double get_in_val(uint8_t in_num, block_handle_t* block) {
    if (!block || in_num >= block->in_cnt) {
        return 0.0;
    }
    data_types_t type = block->in_data_type_table[in_num];
    void* src = (uint8_t*)block->in_data + block->in_data_offsets[in_num];
    
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

/*macros if datatype at input is the same in all blocks*/
#define IN_GET_UI8(block, idx)   \
    ((block)->in_data_type_table[(idx)] == DATA_UI8 ? \
     *((uint8_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (uint8_t)0)

#define IN_GET_UI16(block, idx)  \
    ((block)->in_data_type_table[(idx)] == DATA_UI16 ? \
     *((uint16_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (uint16_t)0)

#define IN_GET_UI32(block, idx)  \
    ((block)->in_data_type_table[(idx)] == DATA_UI32 ? \
     *((uint32_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (uint32_t)0)

#define IN_GET_I8(block, idx)    \
    ((block)->in_data_type_table[(idx)] == DATA_I8 ? \
     *((int8_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (int8_t)0)

#define IN_GET_I16(block, idx)   \
    ((block)->in_data_type_table[(idx)] == DATA_I16 ? \
     *((int16_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (int16_t)0)

#define IN_GET_I32(block, idx)   \
    ((block)->in_data_type_table[(idx)] == DATA_I32 ? \
     *((int32_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : (int32_t)0)

#define IN_GET_F(block, idx)     \
    ((block)->in_data_type_table[(idx)] == DATA_F ? \
     *((float*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : 0.0f)

#define IN_GET_D(block, idx)     \
    ((block)->in_data_type_table[(idx)] == DATA_D ? \
     *((double*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) : 0.0)

#define IN_GET_B(block, idx)     \
    ((block)->in_data_type_table[(idx)] == DATA_B ? \
     *((uint8_t*)((uint8_t*)(block)->in_data + (block)->in_data_offsets[(idx)])) != 0 : 0)


static inline void q_set_value(block_handle_t* block, uint8_t q_id, double val)
{
    if (!block || q_id >= block->q_cnt) return;

    void* dst = (uint8_t*)block->q_data + block->q_data_offsets[q_id];
    data_types_t type = block->q_data_type_table[q_id];

    switch(type) {
        case DATA_UI8:  
            val = round(val);
            if (val < 0) val = 0; 
            if (val > UINT8_MAX) val = UINT8_MAX; 
            *((uint8_t*)dst)  = (uint8_t)val; 
            break;

        case DATA_UI16: 
            val = round(val);
            if (val < 0) val = 0; 
            if (val > UINT16_MAX) val = UINT16_MAX; 
            *((uint16_t*)dst)= (uint16_t)val; 
            break;

        case DATA_UI32: 
            val = round(val);
            if (val < 0) val = 0; 
            if (val > UINT32_MAX) val = UINT32_MAX; 
            *((uint32_t*)dst)= (uint32_t)val; 
            break;

        case DATA_I8:   
            val = round(val);
            if (val < INT8_MIN) val = INT8_MIN; 
            if (val > INT8_MAX) val = INT8_MAX; 
            *((int8_t*)dst)  = (int8_t)val; 
            break;

        case DATA_I16:  
            val = round(val);
            if (val < INT16_MIN) val = INT16_MIN; 
            if (val > INT16_MAX) val = INT16_MAX; 
            *((int16_t*)dst)= (int16_t)val; 
            break;

        case DATA_I32:  
            val = round(val);
            if (val < INT32_MIN) val = INT32_MIN; 
            if (val > INT32_MAX) val = INT32_MAX; 
            *((int32_t*)dst)= (int32_t)val; 
            break;

        case DATA_F:    
            *((float*)dst) = (float)val; 
            break;

        case DATA_D:    
            *((double*)dst)= val;   
            break;

        case DATA_B:    
            *((uint8_t*)dst)= (val != 0.0) ? 1 : 0; 
            break;
    }
}
