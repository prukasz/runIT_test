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


/**
 * @brief Main function for clock block 
 * @param block Handle to the block instance
 * @return emu_result_t.cxode EMU_OK on success, error code otherwise
 * @note Inputs: EN[0], PERIOD[1], WIDTH[2]
 * @note Outputs: Q[0]
 * @note Packet [packet_id:u8] + cfg: ([period:u32][width:u32])

 */
emu_result_t block_clock(block_handle_t block);


/**
 * @brief Parser function for clock block configuration
 * @param packet_data Pointer to the packet data with configuration
 * @param packet_len Length of the packet data
 * @param block_ptr Pointer to the block instance to configure
 * @return emu_result_t.code EMU_OK on success, error code otherwise
 * @note Packet [packet_id:u8] + cfg: ([period:u32][width:u32])
 * @note Packet ID: BLOCK_PKT_CFG (0x01)
 * @example Packet data for config: [0x01][0xE8 0x03 0x00 0x00][0xF4 0x01 0x00 0x00] (Period=1000ms, Width=500ms)
 */
emu_result_t block_clock_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);

/**
 * @brief Verification function for clock block configuration
 * @param block Handle to the block instance to verify
 * @return emu_result_t.code EMU_OK on success, error code otherwise
 * @note Checks if the configuration parameters are valid and exists 
 */
emu_result_t block_clock_verify(block_handle_t block);

/**
 * @brief Free function for clock block
 * @param block Handle to the block instance to free
 */
void block_clock_free(block_handle_t block);


