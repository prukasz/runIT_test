#include "emulator_variables_acces.h"
typedef enum{
    OP_VAR   = 0x00,      
    OP_CONST = 0x01,      
    OP_ADD   = 0x02,     
    OP_MUL   = 0x03,      
    OP_DIV   = 0x04,      
    OP_COS   = 0x05,     
    OP_SIN   = 0x06,      
    OP_POW   = 0x07,      
    OP_ROOT  = 0x08,      
    OP_SUB   = 0x09, 
}op_code_t;
     

typedef struct {
    op_code_t op;
    uint8_t input_index; //for operator its 00, for constant index in constant table
} instruction_t;


typedef struct{
    instruction_t *code;
    uint8_t count;
    double *constant_table;
} expression_t;

emu_result_t emu_parse_block_math(chr_msg_buffer_t *source, block_handle_t *block);
void emu_parse_block_math_free(block_handle_t* block);




/****************************************************************************
                    MATH BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]ENO   -->
    -->VAL  [1]|OPT             |[1]RESULT-->
    -->VAL  [2]|OPT    MATH     |
    -->VAL  [3]|OPT....         |
               |________________|
 
****************************************************************************/

/**
*@brief compute block - handling math operations
*/
emu_result_t block_math(block_handle_t *src);
