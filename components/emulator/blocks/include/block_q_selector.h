#pragma once
#include "emu_variables_acces.h"


/****************************************************************************
                    Q SELECTOR BLOCK
                ________________
    -->EN   [0]|BOOL      [0]| -->
    -->SEL  [1]|UINT8_T   [1]| -->
               |          [2]| -->
               |          [N]| -->
               |________________|
 
****************************************************************************
DESCRIPTION:
Demultiplexer / Switch-Case block. Activates ONE of N boolean outputs.
Based on selector value, sets the selected output to TRUE and all others to FALSE.
Only the selected output instance is marked as "updated".

INPUTS:
- EN (BOOL): Enable input. Block only executes when EN is true.
- SEL (UINT8_T): Selector input that determines which output is active (0..N-1).

OUTPUTS:
- [0]..[N]: Boolean outputs. Only one is TRUE based on selector value.

BEHAVIOR:
- If SEL = 0 → out[0]=true,  out[1]=false, out[2]=false, ...
- If SEL = 1 → out[0]=false, out[1]=true,  out[2]=false, ...
- If SEL = 2 → out[0]=false, out[1]=false, out[2]=true,  ...
- If SEL >= N → all outputs = false (out of bounds)

NOTE: 
Similar to a switch-case statement in C. Use when you need to trigger ONE of
multiple code paths based on a selector value.
*/

emu_result_t block_q_selector(block_handle_t block);
