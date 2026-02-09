#pragma once
#include "emu_variables_acces.h"


/****************************************************************************
                    SET BLOCK
                 ________________
     -->EN   [0]|BOOL            |
     -->VAL  [1]|                |
    -->TARGET[2]|        SET     |
                |                |
                |________________|
DESCRIPTION:
SET VAL TO TARGET (no output, write-only block)
INPUTS:
- EN (BOOL): Enable input. Block only executes when EN is true.
- VAL (ANY): Value to be set. Can be of any type.
- TARGET (ANY): Target variable to set. Can be of any type but should be compatible with VAL for speed.
OUTPUTS:
- None (SET block has no outputs, including no ENO)
****************************************************************************/



/**
 * @brief implementation of SET block
 */
emu_result_t block_set(block_handle_t block_data);


