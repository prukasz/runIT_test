
#pragma once
#include "emulator_blocks.h"
#include "emulator_errors.h"

/* Timer Types */
typedef enum {
    TIMER_TYPE_TON = 0x01, // On-Delay
    TIMER_TYPE_TOF = 0x02, // Off-Delay
    TIMER_TYPE_TP  = 0x03  // Pulse
} block_timer_type_t;

/* Block Data (Config + Runtime State) */
typedef struct {
    /* Configuration (Parsed from file) */
    block_timer_type_t type;
    uint32_t default_pt;    // Default Preset Time if Input[1] is not linked

    /* Runtime State (Persists across cycles) */
    uint32_t elapsed_time;  // Current accumulated time [ms]
    bool q_out;             // Current Output state
    bool prev_in;           // Previous input state (for edge detection)
    bool active;            // Internal activity flag (for TP/TOF)
} block_timer_t;

/* Inputs/Outputs Indices */
#define BLOCK_TIMER_IN_EN    0
#define BLOCK_TIMER_IN_PT    1
#define BLOCK_TIMER_IN_RST   2

#define BLOCK_TIMER_OUT_Q    0
#define BLOCK_TIMER_OUT_ET   1

/* Functions */


/****************************************************************************
                   TIMER BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]ENO         -->
    -->TIME [1]|[ms]        [ms]|[1]ELAPSED TIME-->
    -->RESET[2]|BOOL     TIMER  |
               |                |
               |________________|
 
****************************************************************************/
emu_result_t block_timer(block_handle_t *block);

emu_result_t emu_parse_block_timer(chr_msg_buffer_t *source, uint16_t block_idx);