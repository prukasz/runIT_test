#pragma once
#include "emu_variables_acces.h"
#include "emu_logging.h"
#include "emu_blocks.h"

/****************************************************************************
                COUNTER BLOCK
                ________________
--> CTU    [0] |BOOL        BOOL|[0]ENO   -->
--> CTD    [1] |BOOL      DOUBLE|[1]VAL   -->
--> RST    [2] |BOOL            |  
--> STEP   [3] |OPT             |
--> LIM_MAX[4] |OPT             |
--> LIM_MIN[5] |OPT             |
               |                |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block implements a counter that can count up and down based on input signals.
It supports reset functionality and allows dynamic adjustment of step size and limits.
INPUTS:
- EN (Boolean (ANY)): Enables or disables the counter. If false, the counter will not count.
- CTU (Boolean (ANY)): Count up input. When true, the counter increments by the step value.
- CTD (Boolean (ANY)): Count down input. When true, the counter decrements by the step value.
- RST (Boolean (ANY)): Reset input. When true, the counter resets to the initial value.
- STEP (ANY): The value by which the counter increments or decrements.
- LIM_MAX (ANY): The maximum limit for the counter value.
- LIM_MIN (ANY): The minimum limit for the counter value.
OUTPUTS:
- Q (Boolean): The clock output signal, which toggles between true and false based on the specified period and width.
- VAL (Double): The current value of the counter.
NOTE:
Counter will not exceed the specified maximum and minimum limits.
Counter can be configured to count on rising edge or when input is active.
Rising edge requires FALSE->TRUE cylce

USAGE:
mode on rising edge:
[GPIO_1]->[CTU][COUNTER][VAL]->[SET]("button_press_count")

mode when active:
[GPIO_2]->[CTU][COUNTER][VAL]->[SET]("button_press_count")

RESULTS:
When GPIO_1 rising edge, the COUNTER block will increment the VAL output by the STEP value, up to the LIM_MAX limit.
The value is assgned to "button_press_count" variable.

When GPIO_2 is active, the COUNTER block will increment the VAL output by the STEP value, up to the LIM_MAX limit.
Counter will increment each loop cycle while GPIO_2 is active.

***************************************************************************/


emu_result_t block_counter(block_handle_t block);
emu_result_t block_counter_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
emu_result_t block_counter_verify(block_handle_t block);
void block_counter_free(block_handle_t block);

