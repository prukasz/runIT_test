#pragma once
#include "emu_variables_acces.h"


/****************************************************************************
                    SET BLOCK
                 ________________
     -->VAL  [0]|                |
    -->TARGET[1]|                |
                |        SET     |
                |                |
                |________________|
DESCRIPTION:
SET VAL TO TAERGET
INPUTS:
- VAL (ANY): Value to be set. Can be of any type.
- TARGET (ANY): Target variable to set. Can be of any type but should be compatible with VAL for speed
OUTPUTS:
****************************************************************************/



/**
 * @brief implementation of SET block
 */
emu_result_t block_set(block_handle_t block_data);


