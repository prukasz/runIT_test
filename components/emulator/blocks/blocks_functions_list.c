#include "blocks_functions_list.h"


/**
 * @brief Tables with main functions for each block: requierd for all block types
 */
emu_block_func blocks_main_functions_table[255]={
    [BLOCK_LOGIC]=block_logic,
    [BLOCK_MATH]=block_math,
    [BLOCK_FOR]=block_for,
    [BLOCK_TIMER]=block_timer,
    [BLOCK_SET]=block_set,
    [BLOCK_COUNTER]=block_counter,
    [BLOCK_CLOCK]=block_clock,
    [BLOCK_IN_SELECTOR]=block_in_selector,
    [BLOCK_Q_SELECTOR]=block_q_selector
};


/**
 * @brief Table for block specific parsers
 */
emu_block_parse_func emu_block_parsers_table[255]={
    [BLOCK_LOGIC]=block_logic_parse,
    [BLOCK_MATH]=block_math_parse,
    [BLOCK_FOR]=block_for_parse,
    [BLOCK_TIMER]=block_timer_parse,
    [BLOCK_COUNTER]=block_counter_parse,
    [BLOCK_CLOCK]=block_clock_parse,
};
/**
 * @brief Table for block specific free functions (cleanup/reset)
 */
emu_block_free_func emu_block_free_table[255]={
    [BLOCK_LOGIC]=block_logic_free,
    [BLOCK_MATH]=block_math_free,
    [BLOCK_FOR]=block_for_free,
    [BLOCK_TIMER]=block_timer_free,
    [BLOCK_COUNTER]=block_counter_free,
    [BLOCK_CLOCK]=block_clock_free,
};

/**
 * @brief Table for block specific verify functions (check configuration validity and code completeness)
 */
emu_block_verify_func emu_block_verify_table[255]={
    [BLOCK_LOGIC]=block_logic_verify,
    [BLOCK_MATH]=block_math_verify,
    [BLOCK_FOR]=block_for_verify,
    [BLOCK_TIMER]=block_timer_verify,
    [BLOCK_COUNTER]=block_counter_verify,
    [BLOCK_CLOCK]=block_clock_verify
};


