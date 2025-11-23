#pragma once
#include "gatt_buff.h"
#include "string.h"
#include "stdbool.h"
#include "emulator_types.h"
#include "emulator_variables.h"

typedef struct{
    bool can_allocate_var;
    bool can_fill_var;
    bool can_create_blocks;
    bool can_fill_blocks;
    bool can_run_code;
    bool finished;
}parse_status_t;

extern parse_status_t parse_status;

#define PARSE_DONE_ALLOCATE_VAR()        (parse_status.can_allocate_var)
#define PARSE_SET_ALLOCATE_VAR(x)       (parse_status.can_allocate_var = (x))

#define PARSE_FINISHED()            (parse_status.finished)
#define PARSE_SET_FINISHED(x)       (parse_status.finished = (x))

#define PARSE_DONE_FILL_VAR()        (parse_status.can_fill_var)
#define PARSE_SET_FILL_VAR(x)       (parse_status.can_fill_var = (x))

#define PARSE_DONE_CREATE_BLOCKS()   (parse_status.can_create_blocks)
#define PARSE_SET_CREATE_BLOCKS(x)  (parse_status.can_create_blocks = (x))

#define PARSE_DONE_FILL_BLOCKS()     (parse_status.can_fill_blocks)
#define PARSE_SET_FILL_BLOCKS(x)    (parse_status.can_fill_blocks = (x))

#define PARSE_CAN_RUN_CODE()        (parse_status.can_run_code)
#define PARSE_SET_RUN_CODE(x)       (parse_status.can_run_code = (x))



bool _check_header(uint8_t *data, emu_header_t header);
bool _check_arr_packet_size(uint16_t len, uint8_t step);
bool _check_arr_header(uint8_t *data, uint8_t *step);

emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem);
emu_err_t emu_parse_into_variables(chr_msg_buffer_t *source, emu_mem_t *mem);
emu_err_t emu_parse_block(chr_msg_buffer_t *source);
