#pragma once
#include "emulator.h"
#include "emulator_types.h"
#include "emulator_variables.h"
#include "gatt_buff.h"
#include "string.h"
#include "stdbool.h"

typedef struct{
    bool can_allocate;
    bool can_fill_values;
    bool can_run_code;
}parse_status_t;

extern parse_status_t parse_status;
#define PARSE_CAN_ALLOCATE() ({parse_status.can_allocate;})
#define PARSE_SET_ALLOCATE(bool) ({parse_status.can_allocate = (bool);})
#define PARSE_CAN_FILL_VAL() ({parse_status.can_fill_values;})
#define PARSE_SET_FILL_VAL(bool) ({parse_status.can_fill_values = (bool);})
#define PARSE_CAN_RUN_CODE() ({parse_status.can_run_code;})
#define PARSE_SET_RUN_CODE(bool) ({parse_status.can_run_code = (bool);})

bool _check_header(uint8_t *data, emu_header_t header);
bool _check_arr_packet_size(uint16_t len, uint8_t step);
bool _check_arr_header(uint8_t *data, uint8_t *step);

emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem);
emu_err_t emu_parse_into_variables(chr_msg_buffer_t *source, emu_mem_t *mem);