#pragma once 
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h"

typedef struct {
    double   default_period;
    double   default_width;
    uint64_t start_time_ms; // Czas włączenia (dla synchronizacji fazy)
    bool     prev_en;       // Do wykrycia zbocza EN (restart cyklu)
} block_clock_handle_t;


emu_result_t block_clock(block_handle_t *block);
emu_result_t block_clock_parse(chr_msg_buffer_t *source, block_handle_t *block);
emu_result_t block_clock_verify(block_handle_t *block);
void block_clock_free(block_handle_t* block);

