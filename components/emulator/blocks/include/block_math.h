#include "utils_block_in_q_access.h"
#include "utils_global_access.h"

#define READ_U16(DATA, IDX)     ((uint16_t)((DATA)[(IDX)]) | ((uint16_t)((DATA)[(IDX)+1]) << 8))

typedef enum {
    OP_VAR = 0x00,
    OP_ADD = 0x01,
    OP_MUL = 0x02,
    OP_DIV = 0x03,
    OP_SUB = 0x04,
    OP_ROOT = 0x05,
    OP_POWER = 0x06,
    OP_SIN = 0x07,
    OP_COS = 0x08,
    OP_CONST = 0x09,
} op_code_t;

typedef struct {
    op_code_t op;
    uint8_t input_index; //for operator its 00, for constant index in constant table
} instruction_t;


typedef struct{
    instruction_t *code;
    uint8_t count;
    double *constant_table;
} expression_t;

typedef emu_err_t (*emu_block_func)(block_handle_t *block);

emu_err_t emu_parse_math_block(chr_msg_buffer_t *source, size_t msg_index);
emu_err_t __emu_parse_math_expr(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression);
emu_err_t __emu_parse_math_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, instruction_t* code);
emu_err_t __emu_parse_math_const(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt);
emu_err_t __emu_parse_math_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table);

/**
*@brief compute block - handling math operations
*/
emu_err_t block_math(block_handle_t *src);
