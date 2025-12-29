#pragma once

#include "emulator_errors.h"
#include "emulator_blocks.h"

emu_result_t debug_blocks_value_dump(block_handle_t** block_struct_list, uint16_t total_block_cnt);
emu_result_t emu_debug_output_add(chr_msg_buffer_t * msg);