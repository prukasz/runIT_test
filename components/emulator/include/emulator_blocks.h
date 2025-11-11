#pragma once
#include "emulator_types.h"
#include "emulator.h"
typedef emu_err_t (*emu_block_func)(block_handle_t *block);



emu_err_t block_compute(block_handle_t* block);

void block_fill_results(block_handle_t* block);







