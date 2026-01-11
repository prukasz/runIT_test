#pragma once 
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h"


emu_result_t block_clock(block_handle_t *block);
emu_result_t block_clock_parse(chr_msg_buffer_t *source, block_handle_t *block);
emu_result_t block_clock_verify(block_handle_t *block);
void block_clock_free(block_handle_t* block);

