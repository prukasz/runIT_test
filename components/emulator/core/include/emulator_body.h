#pragma once

/**
 * @brief allocate struct list for every block, also allocate function list
 * @param num_blocks total blocks in code
 */
emu_err_t emu_create_block_tables(uint16_t num_blocks);

/**
*@brief This task runs all blocks parsed and is managed by internally by emulator loop
*/
void emu_body_loop_task(void* params);
