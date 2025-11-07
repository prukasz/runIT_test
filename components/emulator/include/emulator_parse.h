#pragma once
#include "emulator.h"
#include "emulator_types.h"
#include "emulator_variables.h"
#include "gatt_buff.h"

bool _check_header(uint8_t *data, emu_order_t header);
bool _check_arr_packet_size(uint16_t len, uint8_t step);
bool _check_arr_header(uint8_t *data, uint8_t *step);
emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem, uint8_t *emu_mem_size);