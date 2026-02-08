#pragma once
#include "block_math.h"
#include "block_logic.h"
#include "block_set.h"
#include "block_for.h"
#include "block_timer.h"
#include "block_counter.h"
#include "block_clock.h"
#include "block_in_selector.h"

/***********************************************************************************
 * Those tables contains main functions, parsers, free functions and verify functions
 * for each block type supported by emulator 
 * When new block is added, corresponding functions must be added to those tables if block requires them
 * For some blocks, certain functions can be set to NULL if not required 
 * 
 * This design eliminates the need for large switch-case statements, improving code maintainability and scalability
 * see blocks_functions_list.c for tables definitions
 * ***********************************************************************************/

extern emu_block_func blocks_main_functions_table[255];
extern emu_block_parse_func emu_block_parsers_table[255];
extern emu_block_free_func emu_block_free_table[255];
extern emu_block_verify_func emu_block_verify_table[255];



