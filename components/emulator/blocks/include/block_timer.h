
#pragma once
#include "emulator_blocks.h"
#include "emulator_errors.h"

/* Timer Types */
typedef enum {
    TIMER_TYPE_TON = 0x01, // On-Delay
    TIMER_TYPE_TOF = 0x02, // Off-Delay
    TIMER_TYPE_TP  = 0x03,  // Pulse
    TIMER_TYPE_TON_INV = 0x04,
    TIMER_TYPE_TOF_INV = 0x05,
    TIMER_TYPE_TP_INV = 0x06
} block_timer_type_t;


typedef struct {
    block_timer_type_t type;
    uint64_t start_time;
    uint32_t default_pt; 
    uint32_t delta_time;  
    bool q_out;            
    bool prev_in;                  
    bool counting;         
} block_timer_t;

/* Inputs/Outputs Indices */
#define BLOCK_TIMER_IN_EN    0
#define BLOCK_TIMER_IN_PT    1
#define BLOCK_TIMER_IN_RST   2

#define BLOCK_TIMER_OUT_Q    0
#define BLOCK_TIMER_OUT_ET   1  //elapsed time 

/* Functions */


/****************************************************************************
                   TIMER BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]Q           -->
    -->TIME [1]|[ms]        [ms]|[1]ELAPSED TIME-->
    -->RESET[2]|BOOL     TIMER  |
               |                |
               |________________|
 
****************************************************************************/
emu_result_t block_timer(block_handle_t *block);

emu_result_t block_timer_parse(chr_msg_buffer_t *source, block_handle_t *block);
void block_timer_free(block_handle_t *block);
emu_result_t block_timer_verify(block_handle_t *block);