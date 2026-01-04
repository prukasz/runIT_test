#include "emulator_blocks_functions_list.h"

emu_block_func blocks_functions_table[255]={
    [BLOCK_LOGIC]=block_logic,
    [BLOCK_MATH]=block_math,
    [BLOCK_FOR]=block_for,
    [BLOCK_TIMER]=block_timer
};

emu_block_parse_func emu_block_parsers_table[255]={
    [BLOCK_LOGIC]=emu_parse_block_logic,
    [BLOCK_MATH]=emu_parse_block_math,
    [BLOCK_FOR]=emu_parse_block_for,
    [BLOCK_TIMER]=emu_parse_block_timer

};