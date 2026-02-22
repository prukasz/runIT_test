#pragma once
#include "emu_variables_acces.h"
#include "block_types.h"

/****************************************************************************
                    LATCH BLOCK
                ________________
    -->EN   [0]|            BOOL|[0]ENO     -->
    -->S    [1]|                |
    -->R    [2]|  SR/RS LATCH   |
               |                |
               |________________|
 
****************************************************************************/

/**
 * @brief implementation of LATCH block
 */
emu_result_t block_latch(block_handle_t block_data);

emu_result_t block_latch_verify(block_handle_t block);

void block_latch_free(block_handle_t block);

emu_result_t block_latch_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr);