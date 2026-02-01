
#pragma once
#include "emulator_blocks.h"
#include "emulator_logging.h"

/* Functions */


/****************************************************************************
                   TIMER BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]Q           -->
    -->PT   [1]|[ms]        [ms]|[1]ELAPSED TIME-->
    -->RESET[2]|BOOL     TIMER  |
               |                |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block implements various timer functions commonly used in PLC programming.
TYPES:
- TON (On-Delay Timer): Delays turning on the output Q after the input EN becomes true for a specified preset time PT.
- TOF (Off-Delay Timer): Delays turning off the output Q after the input EN becomes false for a specified preset time PT.
- TP (Pulse Timer): Generates a pulse on the output Q for a specified preset time PT when the input EN transitions from false to true.
NOTE: 
- When the RESET input is true, the timer resets its state, and the output Q is set according to the timer type.
- The ELAPSED TIME output indicates the time that has elapsed since the timer started counting.
- The timer operates based on the system time provided by the emulator loop.
- TP requires rising edge on EN input to start pulse. so when EN is constantly high TP will not retrigger again.
INPUTS:
- EN (Boolean (ANY)): Enables or disables the timer. If false, the output Q will be false (except for TOF).
- PT (Unsigned Integer (ms)): Preset time in milliseconds for the timer operation.
- RESET (Boolean (ANY)): Resets the timer state when true.
OUTPUTS:
- Q (Boolean (ANY)): The output of the timer, which changes state based on the timer type and inputs.
- ELAPSED TIME (Unsigned Integer (ms)): The elapsed time since the timer started counting.
NOTE:
There are also INVERTED versions of each timer type:
- TON_INV
- TOF_INV
- TP_INV
Those inverted types reverse the value of the Q output.
so 
TON_INV will set Q=true when EN is false for PT time.
TOF_INV will set Q=false when EN is true for PT time.
TP_INV will set Q=true for PT time when EN goes from true to false (negative edge).

USAGE:
[TIMER.cfg type=TON PT=5000ms]
[GPIO_1]->[EN][TIMER][Q]->["Run_component"]

RESULTS:
When GPIO_1 is set to true, the TIMER block will wait for 5000 ms before setting the Q output to true and holding it true as long as EN is true.
When GPIO_1 is set to false, the Q output will immediately go false (for TON type).


*******************************************************************************/
emu_result_t block_timer(block_handle_t block);

emu_result_t block_timer_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
void block_timer_free(block_handle_t block);
emu_result_t block_timer_verify(block_handle_t block);