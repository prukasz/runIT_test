#include "block_logic.h"
#include "emulator_errors.h"
#include "utils_parse.h"
#include <math.h>
#include <float.h>
#include <string.h>


extern block_handle_t **emu_block_struct_execution_list;
static const char* TAG = "CMP_PARSER";

static inline bool is_true(double a);
static inline double bool_to_double(bool val);

static emu_err_t _emu_parse_logic_expr(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt);
static emu_err_t _emu_parse_logic_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, logic_instruction_t* code);
static emu_err_t _emu_parse_logic_const(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt);
static emu_err_t _emu_parse_logic_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table);
static emu_err_t _clear_logic_expression_internals(logic_expression_t* expr);


emu_result_t emu_parse_block_logic(chr_msg_buffer_t *source, uint16_t block_idx)
{
    emu_result_t res = {.code = EMU_OK};
    uint8_t *data;
    size_t len;
    block_handle_t *block_ptr;
    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx = 0;

    // 1. Parsowanie wyrażeń (Header: 0x03)
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (len > 3 && data[0] == 0xbb && data[1] == BLOCK_CMP && data[2] == 0x02 && (READ_U16(data, 3) == block_idx))
        {
            LOG_I(TAG, "Detected CMP Expression header");
            uint16_t block_idx = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_idx];
            
            block_ptr->extras = calloc(1, sizeof(logic_expression_t));
            if(!block_ptr->extras){
                ESP_LOGE(TAG, "No memory for CMP block idx: %d)", block_idx);
                res.code = EMU_ERR_NO_MEM;
                res.block_idx = block_idx;
                res.restart = true;
                return res;
            }

            size_t const_msg_cnt = 1;
            if(_emu_parse_logic_expr(source, search_idx, (logic_expression_t*)(block_ptr->extras), &const_msg_cnt) != EMU_OK){
                ESP_LOGE(TAG, "Error parsing CMP expression block idx: %d", block_idx);
                emu_logic_block_free_expression(block_ptr);
                res.code = EMU_ERR_NO_MEM;
                res.block_idx = block_idx;
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

        if (len > 3 && data[0] == 0xbb && data[1] == BLOCK_CMP && data[2] == 0x01 && (READ_U16(data, 3) == block_idx))
        {
            uint16_t block_idx = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_idx];
            
            if(block_ptr->extras != NULL) {
                LOG_I(TAG, "Detected Constant Table for CMP block %d", block_idx);
                size_t const_msg_cnt = 1;
                if(_emu_parse_logic_const(source, search_idx, (logic_expression_t*)(block_ptr->extras), &const_msg_cnt) != EMU_OK){
                     ESP_LOGE(TAG, "Error parsing constants for CMP block %d", block_idx);
                     emu_logic_block_free_expression(block_ptr);
                     res.code = EMU_ERR_NO_MEM;
                     res.block_idx = block_idx;
                     res.restart = true;
                     return res;
                }
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


emu_result_t block_logic(block_handle_t* block){
    emu_result_t res = {.code = EMU_OK};

    logic_expression_t* eval = (logic_expression_t*)block->extras;
    double stack[16];
    int8_t over_top = 0;
    double result = 0;
    double val_a, val_b; 

    for(int8_t i = 0; i < eval->count; i++){
        logic_instruction_t *ins = &(eval->code[i]);
          
        switch(ins->op){
            case CMP_OP_VAR:
                utils_get_in_val_auto(block, ins->input_index, &stack[over_top++]);
                break;

            case CMP_OP_CONST:
                stack[over_top++] = eval->constant_table[ins->input_index];
                break;
              
            case CMP_OP_GT:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(val_a > val_b); 
                over_top--;
                break;

            case CMP_OP_LT:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(val_a < val_b);
                over_top--;
                break;

            case CMP_OP_EQ:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(fabs(val_a - val_b) < DBL_EPSILON);
                over_top--;
                break;

            case CMP_OP_GTE:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(val_a >= val_b); 
                over_top--;
                break;

            case CMP_OP_LTE:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(val_a <= val_b);
                over_top--;
                break;    
            
            case CMP_OP_AND:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(is_true(val_a) && is_true(val_b));
                over_top--;
                break;

            case CMP_OP_OR:
                val_b = stack[over_top-1];
                val_a = stack[over_top-2];
                stack[over_top-2] = bool_to_double(is_true(val_a) || is_true(val_b));
                over_top--;
                break;
            case CMP_OP_NOT: 
                val_a = stack[over_top-1];
                stack[over_top-1] = (val_a > 0.5) ? 0.0 : 1.0;
                break;

            default:
                LOG_E("BLOCK_CMP", "Unknown opcode: %d", ins->op);
                res.code = EMU_ERR_INVALID_DATA;
                res.block_idx = block->block_idx;
                res.abort = true;
                return res;
        }
    }

    if (over_top > 0) {
        result = stack[0];
    } else {
        result = 0.0;
    }

    bool final = is_true(result);
    LOG_I("BLOCK_CMP", "Result: %d", final);

    if(final){
        utils_set_q_val(block, 0, &final);
        block_pass_results(block);
    }
    return res;
}

/* Helper functions for block */
static inline bool is_true(double a){
    return a > 0.5; 
}

static inline double bool_to_double(bool val){
    return val ? 1.0 : 0.0;
}


emu_err_t _clear_logic_expression_internals(logic_expression_t* expr){
    if(expr->code) free(expr->code);
    if(expr->constant_table) free(expr->constant_table);
    return EMU_OK;
}
emu_result_t emu_logic_block_free_expression(block_handle_t* block){
    emu_result_t res = {.code = EMU_OK};
    logic_expression_t* expr = (logic_expression_t*)block->extras;
    if(expr){
        _clear_logic_expression_internals(expr);
        free(expr);
        block->extras = NULL;
        LOG_I(TAG, "Cleared block data");
    }
    return res;
}


static emu_err_t _emu_parse_logic_expr(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt)
{
    uint8_t *data;
    size_t len;
    size_t len_total;
    uint8_t op_count = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    op_count = data[5];
    expression->count = op_count;
    expression->code = (logic_instruction_t*)calloc(op_count, sizeof(logic_instruction_t));
    
    if(!expression->code){
        return EMU_ERR_NO_MEM;
    }
    
    len_total = op_count * 2 + 1;
    _emu_parse_logic_expr_msg(data, len, 6, &idx_start, expression->code);

    while(len_total > (len - 5)){
        len_total = len_total - (len - 5);
        chr_msg_buffer_get(source, msg_index++, &data, &len);
        _emu_parse_logic_expr_msg(data, len, 5, &idx_start, expression->code);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, logic_instruction_t* code)
{
    for(int i = start_index; i < len; i += 2){
        memcpy(&(code[(*idx_start)].op), &data[i], 1);
        memcpy(&(code[(*idx_start)].input_index), &data[i+1], 1);
        (*idx_start)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_const(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt){
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
        return EMU_ERR_NO_MEM;
    }
    _emu_parse_logic_const_msg(data, len, 6, &idx_start, expression->constant_table);

    while(len_total > (len-5)){
        len_total = len_total - (len - 5);
        chr_msg_buffer_get(source, msg_index++, &data, &len);
        _emu_parse_logic_const_msg(data, len, 5, &idx_start, expression->constant_table);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table){
    for(size_t i = start_index; i < len; i = i + sizeof(double)){
        memcpy(&(table[*idx_start]), &data[i], sizeof(double));
        (*idx_start)++;
    }
    return EMU_OK;
}