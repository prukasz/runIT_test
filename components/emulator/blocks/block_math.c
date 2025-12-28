#include "block_math.h"
#include "emulator_errors.h"
#include <math.h>
#include <float.h>
#include <stdbool.h>


extern block_handle_t **emu_block_struct_execution_list;
static inline bool is_greater(double a, double b);
static inline bool is_equal(double a, double b);
static inline bool is_zero(double a);

emu_err_t _emu_parse_math_expr(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt);
emu_err_t _emu_parse_math_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, instruction_t* code);
emu_err_t _emu_parse_math_const(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt);
emu_err_t _emu_parse_math_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table);
emu_err_t _clear_expression_internals(expression_t* expr);

static const char* TAG = "MATH_PARSER";

emu_result_t emu_parse_block_math(chr_msg_buffer_t *source, uint16_t block_idx)
{
    emu_result_t res = {.code = EMU_OK};
    uint8_t *data;
    size_t len;
    block_handle_t *block_ptr;

    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx = 0;


        while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[1] == BLOCK_MATH && data[2] == 0x02 && (READ_U16(data, 3) == block_idx))
        {
            LOG_I(TAG, "Detected Expression header");
            uint16_t block_id = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_id];
            block_ptr->extras = calloc(1, sizeof(expression_t));
            if(!block_ptr->extras){
                ESP_LOGE(TAG, "No memory to allocate math block extras for block %d", block_id);
                res.code = EMU_ERR_NO_MEM;
                res.block_idx = block_id;
                res.restart = true;
                return res;
            }
            size_t const_msg_cnt = 1;
            if(_emu_parse_math_expr(source, search_idx, (block_ptr->extras), &const_msg_cnt) != EMU_OK){
                ESP_LOGE(TAG, "Error while parsing expression for block %d", block_id);
                emu_math_block_free_expression(block_ptr);
                res.code = EMU_ERR_NO_MEM;
                res.block_idx = block_id;
                res.restart = true;
                return res;
            }
            search_idx++;
        }
        else
        {
            search_idx++;
        }
    }
    search_idx = 0;
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[1] == BLOCK_MATH && data[2] == 0x01 && (READ_U16(data, 3) == block_idx))
        {
            LOG_I(TAG, "Detected Constant Table header");
            uint16_t block_id = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_id];
            size_t const_msg_cnt = 1;
            if(_emu_parse_math_const(source, search_idx, (block_ptr->extras), &const_msg_cnt) != EMU_OK){
                ESP_LOGE(TAG, "Error while parsing constant table for block %d", block_id);
                emu_math_block_free_expression(block_ptr);
                res.code = EMU_ERR_NO_MEM;
                res.block_idx = block_id;
                res.restart = true;
                return res;
            }
            search_idx++;
        }
        else
        {
            search_idx++;
        }
    }
    return res;
}

emu_err_t _emu_parse_math_expr(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt)
{
    uint8_t *data;
    size_t len;
    size_t len_total;
    uint8_t op_count = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    op_count = data[5];
    expression->count = op_count;
    expression->code = (instruction_t*)calloc(op_count, sizeof(instruction_t));
    if(!expression->code){
        ESP_LOGE(TAG, "No memory to allocate math expression code");

        return EMU_ERR_NO_MEM;
    }
    len_total = op_count * 2 + 1;
    _emu_parse_math_expr_msg(data, len, 6, &idx_start, expression->code);

    while(len_total>(len-5)){
        len_total = len_total - (len - 5);
        chr_msg_buffer_get(source, msg_index++, &data, &len);
        _emu_parse_math_expr_msg(data, len, 5, &idx_start, expression->code);
        (*const_msg_cnt)++;
    }
        return EMU_OK;
}

emu_err_t _emu_parse_math_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, instruction_t* code)
{
    for(int i = start_index; i<len; i+=2){
        memcpy(&(code[(*idx_start)].op), &data[i], 1);
        memcpy(&(code[(*idx_start)].input_index), &data[i+1], 1);
        (*idx_start)++;
    }
    return EMU_OK;
}

emu_err_t _emu_parse_math_const(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt){

    uint8_t *data;
    size_t len;
    size_t len_total;
    uint8_t const_cnt = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    const_cnt = data[5];
    len_total = const_cnt * sizeof(double) + 1;
    expression->constant_table = (double*)calloc(const_cnt, sizeof(double));

    if(!expression->constant_table){
        ESP_LOGE(TAG, "No memory to allocate math expression constant table");
        return EMU_ERR_NO_MEM;
    }
    _emu_parse_math_const_msg(data, len, 6, &idx_start, expression->constant_table);

    while(len_total>(len-5)){
        len_total = len_total - (len - 5);
        chr_msg_buffer_get(source, msg_index++, &data, &len);
        _emu_parse_math_const_msg(data, len, 5, &idx_start, expression->constant_table);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

emu_err_t _emu_parse_math_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table){
    for(size_t i = start_index; i<len; i=i+sizeof(double)){
        memcpy(&(table[*idx_start]), &data[i], sizeof(double));
        (*idx_start)++;
    }
    return EMU_OK;
}

emu_result_t block_math(block_handle_t* block){
    emu_result_t res = {.code = EMU_OK};

    expression_t* eval = (expression_t*)block->extras;
    double stack[16];
    int over_top = 0;
    double result = 0;

    for(uint16_t i = 0; i<eval->count; i++){
        instruction_t *ins = &(eval->code[i]);
        switch(ins->op){
            case OP_VAR:
                utils_get_in_val_autoselect(ins->input_index, block, &stack[over_top++]);
                LOG_I(TAG, "Pushed variable value: %lf", stack[over_top-1]);
                break;

            case OP_CONST:
                stack[over_top++] = eval->constant_table[ins->input_index];
                break;

            case OP_ADD:
                stack[over_top-2] = stack[over_top-2] + stack[over_top-1];
                over_top--;
                break;

            case OP_MUL:
                stack[over_top-2] = stack[over_top-2] * stack[over_top-1];
                over_top--;
                break;

            case OP_DIV:
                if(is_zero(stack[over_top-1])){
                    res.code = EMU_ERR_BLOCK_DIV_BY_ZERO;
                    res.warning = true;
                    res.block_idx = block->block_idx;
                    return res;
                }
                stack[over_top-2] = stack[over_top-2]/stack[over_top-1];
                over_top--;
                break;

            case OP_COS:
                stack[over_top-1] = cos(stack[over_top-1]);
                break;

            case OP_SIN:
                stack[over_top-1] = sin(stack[over_top-1]);
                break; 

            case OP_POWER:
                stack[over_top-2] = pow(stack[over_top-2], stack[over_top-1]);
                over_top--;
                break;

            case OP_ROOT:
                stack[over_top-1] = sqrt(stack[over_top-1]);
                break;

            case OP_SUB:
                stack[over_top-2] = stack[over_top-2] - stack[over_top-1];
                over_top--;
                break; 
        }
    }

    result = stack[0];
    LOG_I("BLOCK_MATH", "Computed result: %lf", result);
    utils_set_q_val_safe(block, 0, 1);
    utils_set_q_val_safe(block, 1, result);
    block_pass_results(block);
    return res;      
} 

static inline bool is_equal(double a, double b){
    return fabs(a-b)<DBL_EPSILON;
}

static inline bool is_zero(double a){
    return fabs(a)<DBL_EPSILON;
}

static inline bool is_greater(double a, double b){
    return fabs(a-b)>DBL_EPSILON;
}

emu_err_t _clear_expression_internals(expression_t* expr){
    if(expr->code){
        free(expr->code);
    }
    if(expr->constant_table){
        free(expr->constant_table);
    }
    LOG_I("MATH_CLEANUP", "Cleared expression memory");
    return EMU_OK;
}

emu_err_t emu_math_block_free_expression(block_handle_t* block){
    expression_t* expr = (expression_t*)block->extras;
    if(expr){
        _clear_expression_internals(expr);
        free(expr);
        LOG_I("MATH_CLEANUP", "Cleared block extras memory");
    }
    return EMU_OK;
}
