#pragma once
#include "gatt_buff.h"
#include "string.h"
#include "stdbool.h"
#include "emulator_types.h"
#include "emulator_blocks.h"
#include "emulator_variables.h"
#include "utils_global_access.h"
#include "emulator_errors.h"


typedef enum{
    PARSE_RESTART,
    PARSE_CREATE_VARIABLES,
    PARSE_FILL_VARIABLES,
    PARSE_CREATE_BLOCKS_LIST,
    PARSE_CREATE_BLOCKS,
    PARSE_FILL_BLOCKS,
    PARSE_CHECK_CAN_RUN,
    PARSE_IS_CREATE_VARIABLES_DONE,
    PARSE_IS_FILL_VARIABLES_DONE,
    PARSE_IS_CREATE_BLOCKS_DONE,
    PARSE_IS_FILL_BLOCKS_DONE,
}parse_cmd_t;




/**
 * @brief Use this function to invoke parsing of certain part of code
 * @note Also can check status of parsing
 * @return EMU_OK if status or success, EMU_ERR_DENY when status flase, 
 * EMU_ERR_PARSE_INVALID_REQUEST when can't parse 
 * @param cmd 
 */ 

emu_result_t emu_parse_manager(parse_cmd_t cmd);

emu_result_t emu_parse_fill_block_data();


