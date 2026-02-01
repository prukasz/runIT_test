#pragma once 
#include "emu_logging.h"
#include "emu_variables_acces.h"
#include "emu_blocks.h"


/****************************************************************************
                CLOCK BLOCK
                ________________
--> EN     [0] |ANY         BOOL|[0]Q     -->
--> PERIOD [1] |ANY             |
--> FILL   [2] |ANY             |  
               |                |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block generates a clock signal based on a specified period and duty cycle (width).
It has the following inputs and outputs:
INPUTS:
- EN (Boolean (ANY)): Enables or disables the clock output. If false, the output Q will be false.
- PERIOD (ANY): The total period of the clock signal in milliseconds. Default is set during configuration.
- WIDTH (ANY): The duration for which the clock signal remains high (true) within one period. Default is set during configuration.
OUTPUTS:
- Q (Boolean): The clock output signal, which toggles between true and false based on the specified period and width.

NOTE:
- When EN is false, the clock output Q is set to false, and the internal state is reset.
- Period can be set to any value greater than or equal to 1 ms.
- Width can be set to any non-negative value. If width is greater than or equal to period, the output will remain true for the entire period.

NOTE: Real resolution may be limited by the loop time of the emulator.

USAGE:

CLOCK.cfg [ period = 1000 ms width = 500 ms ]

[GPIO_1]->[SR]->[CLOCK]->[GPIO TOGGLE]
[GPIO_2]--^(R)

RESULTS:
When GPIO_1 rising edge, the CLOCK block will output a square wave signal on Q with a period of 1000 ms and a high duration of 500 ms.
Q will toggle between true and false every 500 ms as long as EN is true. 
When GPIO_2 rising edge occurs, the SR block will reset, affecting the output state accordingly.

***************************************************************************/



emu_result_t block_clock(block_handle_t block);
emu_result_t block_clock_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
emu_result_t block_clock_verify(block_handle_t block);
void block_clock_free(block_handle_t block);


