#pragma once 
#include "emulator_blocks.h"
#include "emulator_errors.h"

typedef enum{
    FOR_COND_GT       = 0x01, // >
    FOR_COND_LT       = 0x02, // <
    FOR_COND_GTE      = 0x04, // >=
    FOR_COND_LTE      = 0x05, // <=
}block_for_condition_t;

typedef enum{
    FOR_OP_ADD        = 0x01,
    FOR_OP_SUB        = 0x02,
    FOR_OP_MUL        = 0x03,
    FOR_OP_DIV        = 0x04,
}block_for_operator_t;

 
typedef struct{
    uint16_t chain_len;
    double start_val;
    double end_val;
    double op_step;
    uint8_t what_to_use_mask;
    block_for_condition_t condition;
    block_for_operator_t op;
}block_for_handle_t;    


/****************************************************************************
                    FOR BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]ENO     -->
    -->START[1]|DOUBLE          |[1]ITERATOR-->
    -->STOP [2]|DOUBLE   FOR    |
    -->STEP [3]|DOUBLE          |
               |________________|
 
****************************************************************************/
#define BLOCK_FOR_IN_EN    0
#define BLOCK_FOR_IN_START 1
#define BLOCK_FOR_IN_STOP  2
#define BLOCK_FOR_IN_STEP  3
#define BLOCK_FOR_MAX_CYCLES 1000

emu_err_t block_for(block_handle_t *src);

emu_err_t emu_parse_for_blocks(chr_msg_buffer_t *source);