#pragma once
#include "emu_variables_acces.h"


/****************************************************************************
                    SELECTOR BLOCK
                ________________
    -->SEL  [0]|UINT8_T    [ANY]|[SELECTED] -->
               |                |
               |                |
               |                |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block when provided with varaibles (on parse) mimics its output to be one of provided
instance based on selector value provided on input during execution.
INPUTS:
- SEL (UINT8_T (ANY)): Selector input that determines which variable instance is output.

OUTPUTS:
- SELECTED (ANY): The selected variable instance based on the selector input.
NOTE: 
Remember that instances provided to SELECTOR block must be compatible with blocks connected to its output.
Example: block connected to SELECTED requires bool table of 16 but gets float table of 4 instead


USAGE:

"x" = F 0.2,
"y" = UINT8_T 4,
"z" = INT16_T [1,3,-5,7,9]

parse time:
(   Ref'x',
    Ref'y',
    Ref'z[2]'
)
math exression = "in_1*100"

["setting"]---->[SEL][SELECTOR]-->[IN_1][MATH] --->["output"]


RESULT:
When setting = 0:
"output" = 20
When setting = 1:
"output" = 400
When setting = 2:
"output"= -500
When setting = 3: error: SELECTOR OOB
NOTE: 
Pay attention to connections of output 
WARNING:
Pay attention when trying to pass other blocks outputs that way, as they need to be updated anyway in
loop before being accesed. 
When provided output of other block please verify order of execution first
This block doesn't update state of it's output
*/

emu_result_t block_selector(block_handle_t block);

emu_result_t block_selector_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
void block_selector_free(block_handle_t block);
emu_result_t block_selector_verify(block_handle_t block);
