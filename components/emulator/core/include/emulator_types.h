#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "error_types.h"
#include "mem_types.h"
#include "loop_types.h"
#include "block_types.h"





/**
* @brief Block type identification code
*/
typedef enum{
    BLOCK_MATH = 0x01,
    BLOCK_SET = 0x02,
    BLOCK_LOGIC = 0x03,
    BLOCK_COUNTER = 0x04,
    BLOCK_CLOCK = 0x05,
    BLOCK_FOR = 0x08,
    BLOCK_TIMER = 0x09,
    BLOCK_SELECTOR = 0x0A,
}block_type_t;



