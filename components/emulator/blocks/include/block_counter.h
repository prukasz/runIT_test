#pragma once
#include "emulator_variables_acces.h"
#include "emulator_errors.h"
#include "emulator_blocks.h"

typedef enum{
    CFG_ON_RISING,
    CFG_WHEN_ACTIVE,
}counter_cfg_t;

typedef struct{
    double current_val;
    double step;
    double max;
    double min;
    double start;
    counter_cfg_t cfg;
    bool prev_ctu; 
    bool prev_ctd;
}counter_handle_t;

emu_result_t block_counter(block_handle_t *block);
emu_result_t block_counter_parse(chr_msg_buffer_t *source, block_handle_t *block);
emu_result_t block_counter_verify(block_handle_t *block);
void block_counter_free(block_handle_t* block);
