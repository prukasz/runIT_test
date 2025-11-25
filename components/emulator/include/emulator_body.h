#pragma once
emu_err_t emu_create_block_tables(uint16_t num_blocks);


/**
*@brief This task runs all code parsed and is managed by loop
*/
void emu_body_loop_task(void* params);
