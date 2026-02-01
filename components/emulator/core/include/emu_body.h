#pragma once
#include "block_types.h"
/**
*@brief This task runs all blocks parsed and is managed by internally by emulator loop
*/
void emu_body_loop_task(void* params);

typedef struct code_ctx_s{
    uint16_t total_blocks;
    block_handle_t* blocks_list; 
} code_ctx_s;

typedef  code_ctx_s *emu_code_handle_t;

emu_code_handle_t emu_get_current_code_ctx();



void emu_reset_code_ctx(void);





