#pragma once
#include "gatt_buff.h"
#include "string.h"
#include "stdbool.h"
#include "emulator_types.h"
#include "emulator_variables.h"
#include "utils_global_access.h"
#include "emulator_errors.h"

typedef enum{
    PARSE_RESTART,
    PARSE_CREATE_VARIABLES,
    PARSE_FILL_VARIABLES,
    PARSE_CREATE_BLOCKS,
    PARSE_FILL_BLOCKS,
    PARSE_CHEKC_CAN_RUN,
    PARSE_IS_CREATE_VARIABLES_DONE,
    PARSE_IS_FILL_VARIABLES_DONE,
    PARSE_IS_CREATE_BLOCKS_DONE,
    PARSE_IS_FILL_BLOCKS_DONE,
}parse_cmd_t;


/*check if header match desired*/
bool parse_check_header(uint8_t *data, emu_header_t header);
/*check if packet includes all sizes for arrays*/
bool parse_check_arr_packet_size(uint16_t len, uint8_t step);
/** 
*@brief returns step for parsing, depends on arr dims
*/
bool parse_check_arr_header(uint8_t *data, uint8_t *step);

/**
 * @brief Find variables packets, allocate space, create arrays
 */
emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem);
/**
 * @brief Find variables packets, fill created arrays
 */
emu_err_t emu_parse_variables_into(chr_msg_buffer_t *source, emu_mem_t *mem);
emu_err_t emu_parse_block(chr_msg_buffer_t *source);
emu_err_t _parse_block_global_access(chr_msg_buffer_t *source, uint16_t start, uint16_t block_idx);
emu_err_t emu_parse_block_cnt(chr_msg_buffer_t *source);