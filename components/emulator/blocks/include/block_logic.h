#pragma once
#include "emulator_variables_acces.h"
#include "emulator_logging.h"
#include <stdint.h>
#include <stdbool.h>


emu_result_t block_logic_parse(chr_msg_buffer_t *source, block_handle_t *block);
void block_logic_free(block_handle_t* block);
emu_result_t block_logic_verify(block_handle_t *block);

/****************************************************************************
                            LOGIC BLOCK
             ________________
    -->EN[0]|            BOOL|[0] ENO
    -->IN[1]|OPT         BOOL|[1] RESULT
    -->IN[2]|OPT    LOGIC    |
    -->IN[3]|OPT             |
            |________________|
 
****************************************************************************
DESCRIPTION:
This block performs logical operations based on a user-defined expression.
INPUTS: 
- EN (Boolean (ANY)): Enables or disables the block. If false, the output ENO will be false.
- IN[1..N] (ANY): Inputs used in the logical expression. The number of inputs depends on the expression defined.
OUTPUTS:
- ENO (Boolean): Indicates if the block executed successfully. It is true if EN is true.
- RESULT (Boolean): The result of the logical expression evaluation.
USAGE:
CMP expr = "in_1>in_2"
or we can hardcode limit "in_1>100" then we don't use global variable

[GPIO_1]---->[EN][LOGIC][EN]->["Working"]
["TEMP"]--->[IN1][LOGIC][RESULT]->[GPIO TOGGLE]
["limit"]-->[IN2][LOGIC]

RESULT:
While EN is true "Working" True
While EN is true LOGIC is executed and result of comparision is on output RESULT

NOTE:
BLOCK can have only one input: EN input, then it has hardcoded expression like "100>101"

*****************************************************************************/
emu_result_t block_logic(block_handle_t *block);

