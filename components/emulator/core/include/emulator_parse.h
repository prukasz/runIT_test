#pragma once
#include "gatt_buff.h"
#include "string.h"
#include "stdbool.h"
#include "emulator_types.h"
#include "emulator_variables.h"
#include "utils_global_access.h"

/** 
*@brief flags struct for handling parsing status
*/
typedef struct{
    bool can_allocate_var;
    bool can_fill_var;
    bool can_create_blocks;
    bool can_fill_blocks;
    bool can_run_code;
    bool finished;
}parse_guard_flags_t;


extern parse_guard_flags_t _parse_guard_flags;

/*is parsing and allocation of variable done*/
#define PARSE_DONE_ALLOCATE_VAR()        (_parse_guard_flags.can_allocate_var)
/*set status for parsing and allocation of variable*/
#define PARSE_SET_ALLOCATE_VAR(x)       (_parse_guard_flags.can_allocate_var = (x))

/*is parsing finished*/
#define PARSE_FINISHED()            (_parse_guard_flags.finished)
/*set parsing finished*/
#define PARSE_SET_FINISHED(x)       (_parse_guard_flags.finished = (x))

/*is parsing values into variables done*/
#define PARSE_DONE_FILL_VAR()        (_parse_guard_flags.can_fill_var)
/*set parsing values into variables*/
#define PARSE_SET_FILL_VAR(x)       (_parse_guard_flags.can_fill_var = (x))

/*is parsing and creating block structs done*/
#define PARSE_DONE_CREATE_BLOCKS()   (_parse_guard_flags.can_create_blocks)
/*set parsing and creating block structs*/
#define PARSE_SET_CREATE_BLOCKS(x)  (_parse_guard_flags.can_create_blocks = (x))
/*is parsing of blocks complete*/
#define PARSE_DONE_FILL_BLOCKS()     (_parse_guard_flags.can_fill_blocks)
/*set parsing of blocks*/
#define PARSE_SET_FILL_BLOCKS(x)    (_parse_guard_flags.can_fill_blocks = (x))

/*is parsing complete*/
#define PARSE_CAN_RUN_CODE()        (_parse_guard_flags.can_run_code)
/*set parsing complete*/
#define PARSE_SET_RUN_CODE(x)       (_parse_guard_flags.can_run_code = (x))


/*check if header match desired*/
bool _check_header(uint8_t *data, emu_header_t header);
/*check if packet includes all sizes for arrays*/
bool _check_arr_packet_size(uint16_t len, uint8_t step);
/** 
*@brief returns step for parsing, depends on arr dims
*/
bool _check_arr_header(uint8_t *data, uint8_t *step);

/**
 * @brief Find variables packets, allocate space, create arrays
 */
emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem);
/**
 * @brief Find variables packets, fill created arrays
 */
emu_err_t emu_parse_variables_into(chr_msg_buffer_t *source, emu_mem_t *mem);
emu_err_t emu_parse_block(chr_msg_buffer_t *source);
emu_err_t _parse_block_mem_acces_data(chr_msg_buffer_t *source, uint8_t *start, _global_val_acces_t **store);
