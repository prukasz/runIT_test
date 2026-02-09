#pragma once
#include "emu_variables_acces.h"


/****************************************************************************
                    IN SELECTOR BLOCK
                ________________
    -->EN   [0]|BOOL           |
    -->SEL  [1]|UINT8_T    [ANY]|[SELECTED] -->
    -->OPT1 [2]|                |
    -->OPT2 [3]|                |
    -->OPTN [N]|                |
               |________________|
 
****************************************************************************
DESCRIPTION:
Provide input to be selected as output based on selector value.
INPUTS:
- EN (BOOL): Enable input. Block only executes when EN is true.
- SEL (UINT8_T): Selector input that determines which variable instance is output.
- OPT1..OPTN: Options to select from (any compatible type).
OUTPUTS:
- SELECTED (ANY): The selected variable instance based on the selector input.
NOTE: 
Remember that instances provided to IN SELECTOR block must be compatible with blocks connected to its output.
Example: block connected to SELECTED requires bool table of 16 but gets float table of 4 instead
*/

emu_result_t block_in_selector(block_handle_t block);
