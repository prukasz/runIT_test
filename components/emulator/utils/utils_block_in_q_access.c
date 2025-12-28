#include "utils_block_in_q_access.h"
#include "emulator_variables.h"
#include "emulator_errors.h"
#include "math.h"
#include "esp_log.h"

emu_err_t utils_get_in_val_auto(block_handle_t *block, uint8_t in_idx, double *out){
    if(!block || !out){
        return EMU_ERR_NULL_PTR;
    }
    if(block->in_used & (1 << in_idx)){
        return utils_get_in_val_safe(in_idx, block, out);
    }

    if (!((block->in_global_used) & (1 << in_idx))) {
        return EMU_ERR_MEM_INVALID_IDX;
    }
    uint16_t lower_bits_mask = (1 << in_idx) - 1;
    uint16_t bits_to_count = block->in_global_used & lower_bits_mask;
    uint8_t idx = (uint8_t)__builtin_popcount(bits_to_count);
    return utils_global_var_acces_recursive(block->global_reference[idx], out);
}
    


emu_err_t utils_get_in_val_safe(uint8_t in_num, block_handle_t* block, double* out) {
    static const char* TAG = "GET IN SAFE";
    if (!out||!block){
        LOG_E(TAG, "No block provided or output provided");
        return EMU_ERR_NULL_PTR;
    }else if(in_num >= block->in_cnt){
        LOG_E(TAG, "Tried to acces input out of bounds %d", in_num);
        return EMU_ERR_MEM_INVALID_IDX;
    }
    double val = 0.0;
    data_types_t type = block->in_data_type_table[in_num];
    //set pointer to start of chosen variable, then switch type of variable to know the lenght to copy
    //then cast to double to return 
    void* src = (uint8_t*)block->in_data + block->in_data_offsets[in_num];
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
            LOG_E(TAG, "Invalid type");
            break;
    }
    *out = val;
    return EMU_OK;
}

emu_err_t utils_get_in_val(uint8_t in_num, block_handle_t* block, void* out) {
    static const char* TAG = "GET IN ";
    if (!block || !out) {
        LOG_E(TAG, "No block provided or output provided");
        return EMU_ERR_NULL_PTR;
    } else if (in_num >= block->in_cnt) {
        LOG_E(TAG, "Tried to acces input out of bounds %d", in_num);
        return EMU_ERR_MEM_INVALID_IDX;
    }

    void* src = (uint8_t*)block->in_data + block->in_data_offsets[in_num];
    size_t size_to_copy = data_size(block->in_data_type_table[in_num]);

    if (size_to_copy > 0) {
        memcpy(out, src, size_to_copy);
    }
    return EMU_OK;
}

emu_err_t utils_set_q_val_safe(block_handle_t* block, uint8_t q_num, double val)
{
    static const char *TAG = "SET Q SAFE";
    if (!block){
        LOG_E(TAG, "No block provided");
        return EMU_ERR_NULL_PTR;
    }else if(q_num >= block->q_cnt){
        LOG_E(TAG, "Tried to acces output out of bounds %d", q_num);
        return EMU_ERR_MEM_OUT_OF_BOUNDS;
    }

    LOG_I(TAG, "To Q number %d type %s try to set: %lf", q_num, DATA_TYPE_TO_STR[block->q_data_type_table[q_num]], val);
    void* dst = (uint8_t*)block->q_data + block->q_data_offsets[q_num];
    data_types_t type = block->q_data_type_table[q_num];

    switch(type) {
        case DATA_UI8:  
            val = round(val);
            if (val < 0.0) val = 0; 
            if (val > UINT8_MAX) val = UINT8_MAX; 
            *((uint8_t*)dst)  = (uint8_t)val; 
            break;

        case DATA_UI16: 
            val = round(val);
            if (val < 0.0) val = 0; 
            if (val > UINT16_MAX) val = UINT16_MAX; 
            *((uint16_t*)dst)= (uint16_t)val; 
            break;

        case DATA_UI32: 
            val = round(val);
            if (val < 0.0) val = 0; 
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
    return EMU_OK;
}

emu_err_t utils_set_q_val(block_handle_t* block, uint8_t q_num, void* val) {
    static const char *TAG = "SET Q";
    if (!block || !val) {
        LOG_E(TAG, "No block or value to set provided");
        return EMU_ERR_NULL_PTR;
    } else if (q_num >= block->q_cnt) {
        LOG_E(TAG, "Tried to acces output out of bounds %d", q_num);
        return EMU_ERR_MEM_OUT_OF_BOUNDS;
    }
    LOG_I(TAG, "To Q number %d type %s try to set val of unknown type", q_num, DATA_TYPE_TO_STR[block->q_data_type_table[q_num]]);
    void* dst = (uint8_t*)block->q_data + block->q_data_offsets[q_num];
    size_t size_to_copy = data_size(block->q_data_type_table[q_num]);
    if (size_to_copy > 0) {
        memcpy(dst, val, size_to_copy);
    }
    return EMU_OK;
}








