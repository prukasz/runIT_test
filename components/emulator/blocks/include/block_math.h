#include "emu_variables_acces.h"

emu_result_t block_math_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
void block_math_free(block_handle_t block);
emu_result_t block_math_verify(block_handle_t block);




/****************************************************************************
                    MATH BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]ENO   -->
    -->VAL  [1]|OPT             |[1]RESULT-->
    -->VAL  [2]|OPT    MATH     |
    -->VAL  [3]|OPT....         |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block performs mathematical operations based on a user-defined expression.
INPUTS:
- EN (Boolean (ANY)): Enables or disables the block. If false, the output ENO will be false.
- VAL[1..N] (ANY): Inputs used in the mathematical expression. The number of inputs depends on the expression defined.
OUTPUTS:
- ENO (Boolean): Indicates if the block executed successfully. It is true if EN is true.
- RESULT (Double): The result of the mathematical expression evaluation.

NOTE: 
Block can handle up to 16 inputs and uses Reverse Polish Notation (RPN) for expression evaluation.
Block can have static variables defined in constant table. all of them are double type.
Block performs operations on double precision floating-point numbers.
USAGE:
MATH expr = "in_1*+cos(in_2)"

[GPIO_1]---->[EN][MATH]
["SPEED"]->[VAL1][MATH][RESULT]->["SPEED_CALC"]
["ANGLE"]->[VAL2][MATH]

RESULT:
While EN is true "MATH" is executed and result of expression is on output RESULT
NOTE: 
BLOCK can have only one input: EN input, then it has hardcoded expression like "cos(0.5)+0.2137"

****************************************************************************/

/**
*@brief compute block - handling math operations
*/
emu_result_t block_math(block_handle_t src);

