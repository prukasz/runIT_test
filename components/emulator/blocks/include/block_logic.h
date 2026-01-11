#pragma once
#include "emulator_variables_acces.h"
#include "emulator_logging.h"
#include <stdint.h>
#include <stdbool.h>

/*Aviable operations*/
typedef enum {
    CMP_OP_VAR      = 0x00, 
    CMP_OP_CONST    = 0x01, 

    CMP_OP_GT       = 0x10, // >
    CMP_OP_LT       = 0x11, // <
    CMP_OP_EQ       = 0x12, // ==
    CMP_OP_GTE      = 0x13, // >=
    CMP_OP_LTE      = 0x14, // <=
    CMP_OP_AND      = 0x20, // &&
    CMP_OP_OR       = 0x21, // ||
    CMP_OP_NOT      = 0x22  // !
} logic_op_code_t;

/*Operation struct*/
typedef struct {
    uint8_t op;        
    uint8_t input_index; 
} logic_instruction_t;

/*Whole comparision struct*/
typedef struct {
    logic_instruction_t *code;
    uint8_t count;
    double *constant_table;
} logic_expression_t;

emu_result_t block_logic_parse(chr_msg_buffer_t *source, block_handle_t *block);
void block_logic_free(block_handle_t* block);
emu_result_t block_logic_verify(block_handle_t *block);

/****************************************************************************
                            CMP BLOCK
             ________________
    -->IN[0]|            BOOL|[0] ENO
    -->IN[1]|OPT         BOOL|[1] RESULT
    -->IN[2]|OPT     CMP     |
    -->IN[3]|OPT             |
            |________________|
 
****************************************************************************/
emu_result_t block_logic(block_handle_t *block);

