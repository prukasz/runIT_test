#pragma once 
#include "emulator_blocks.h"
#include "emulator_errors.h"
/******************************************************************************************/
/*Common functions for all blocks*/
/******************************************************************************************/


/**
* @brief This function returns value of selected input or global var assigned to block
*/
emu_err_t utils_get_in_val_autoselect(uint8_t idx, block_handle_t *block, double* out);

/**
* @brief This function returns value of selected input
*/
emu_err_t utils_get_in_val_safe(uint8_t in_num, block_handle_t* block, double* out);

/**
* @brief This function sets setected q value
* @warning val must match type of output accesed
*/
emu_err_t utils_get_in_val(uint8_t in_num, block_handle_t* block, void* val);

/**
* @brief This function sets setected q value
*/
emu_err_t utils_set_q_val_safe(block_handle_t* block, uint8_t q_num, double val);

/**
* @brief This function returns value of selected input of known type 
* @warning out must match type of input accesed
*/
emu_err_t utils_set_q_val(block_handle_t* block, uint8_t q_num, void* val);




/*macros if datatype at input is the same in all blocks of given type*/
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




