#pragma once
#include "stdint.h"
#include "stdbool.h"
#include "emulator_types.h"
#include "emulator_blocks.h"

#define HEADER_SIZE 2

#define READ_U16(DATA, IDX)     ((uint16_t)((DATA)[(IDX)]) | ((uint16_t)((DATA)[(IDX)+1]) << 8))
#define GET_HEADER(data)    ((uint16_t)(((data[0]) << 8) | (data[1])))


/** 
*@brief Check if header of packtet match desired
*@param data Packet data buffer
*@param header Header to compare form emu_header_t
*/
bool parse_check_header(uint8_t *data, emu_header_t header);

/**
* @brief Assign correct block function to block based on it's type
*/
emu_err_t utils_parse_fuction_assign_to_block(block_handle_t *block);