#include "block_math.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h" 
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "BLOCK_MATH";

// Internal Helpers
static inline bool is_greater(double a, double b);
static inline bool is_equal(double a, double b);
static inline bool is_zero(double a);

// Forward Declarations
emu_err_t _emu_parse_math_expr(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt);
emu_err_t _emu_parse_math_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, instruction_t* code);
emu_err_t _emu_parse_math_const(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt);
emu_err_t _emu_parse_math_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table);
emu_err_t _clear_expression_internals(expression_t* expr);

/* ============================================================================
    PARSING LOGIC
   ============================================================================ */

emu_result_t block_math_parse(chr_msg_buffer_t *source, block_handle_t *block)
{
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "Null ptr provided");

    emu_result_t res = {.code = EMU_OK};
    uint8_t *data;
    size_t len;
    
    // Get ID from the block struct provided
    uint16_t block_idx = block->cfg.block_idx;

    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx = 0;

    // --- PASS 1: EXPRESSION CODE (Subtype 0x02) ---
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        // Check header bytes safely
        if (len > 3 && data[0] == 0xBB && data[1] == BLOCK_MATH && data[2] == EMU_H_BLOCK_PACKET_CUSTOM) 
        {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2); 

            if (packet_blk_id == block_idx) 
            {
                LOG_I(TAG, "Detected Expression header for block %d", block_idx);
                
                // Allocate custom data if not exists
                if (!block->custom_data) {
                    block->custom_data = calloc(1, sizeof(expression_t));
                    if(!block->custom_data){EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "No memory for custom_data block %d", block_idx);}
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_math_expr(source, search_idx, (expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                    block_math_free(block);
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Expr parse error block %d", block_idx);
                }
            }
        }
        search_idx++;
    }

    // PASS 2: CONSTANT TABLE (Subtype 0x01) ---
    search_idx = 0;
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (len > 3 && data[0] == 0xBB && data[1] == BLOCK_MATH && data[2] == EMU_H_BLOCK_PACKET_CONST) 
        {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);

            if (packet_blk_id == block_idx) 
            {
                LOG_I(TAG, "Detected Constant Table header for block %d", block_idx);
                
                // Ensure custom_data exists
                if (!block->custom_data) {
                     block->custom_data = calloc(1, sizeof(expression_t));
                     if(!block->custom_data){EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "No memory for custom_data block %d", block_idx);}
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_math_const(source, search_idx, (expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                    //block_math_free(block);
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Const parse error block %d", block_idx);
                }
            }
        }
        search_idx++;
    }
    return res;
}

emu_err_t _emu_parse_math_expr(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt)
{
    uint8_t *data;
    size_t len;
    uint8_t op_count = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    if(len < 6) return EMU_ERR_PACKET_INCOMPLETE;

    op_count = data[5];
    expression->count = op_count;
    
    expression->code = (instruction_t*)calloc(op_count, sizeof(instruction_t));
    if(!expression->code){
        LOG_E(TAG, "Expr code alloc failed");
        return EMU_ERR_NO_MEM;
    }

    size_t len_total = op_count * 2 + 1;
    _emu_parse_math_expr_msg(data, len, 6, &idx_start, expression->code);

    while(len_total > (len - 5)){
        len_total = len_total - (len - 5);
        msg_index++;
        chr_msg_buffer_get(source, msg_index, &data, &len);
        
        if (!data) break; 

        _emu_parse_math_expr_msg(data, len, 5, &idx_start, expression->code);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

emu_err_t _emu_parse_math_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, instruction_t* code)
{
    for(int i = start_index; i < len - 1; i += 2){ 
        code[*idx_start].op = data[i];
        code[*idx_start].input_index = data[i+1];
        (*idx_start)++;
    }
    return EMU_OK;
}

emu_err_t _emu_parse_math_const(chr_msg_buffer_t *source, size_t msg_index, expression_t* expression, size_t *const_msg_cnt){
    uint8_t *data;
    size_t len;
    uint8_t const_cnt = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    if(len < 6) return EMU_ERR_PACKET_INCOMPLETE;

    const_cnt = data[5];
    size_t len_total = const_cnt * sizeof(double) + 1;
    
    expression->constant_table = (double*)calloc(const_cnt, sizeof(double));
    if(!expression->constant_table){
        LOG_E(TAG, "Const table alloc failed");
        return EMU_ERR_NO_MEM;
    }

    _emu_parse_math_const_msg(data, len, 6, &idx_start, expression->constant_table);

    while(len_total > (len - 5)){
        len_total = len_total - (len - 5);
        msg_index++;
        chr_msg_buffer_get(source, msg_index, &data, &len);
        if(!data) break;

        _emu_parse_math_const_msg(data, len, 5, &idx_start, expression->constant_table);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

emu_err_t _emu_parse_math_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table){
    for(size_t i = start_index; i <= len - sizeof(double); i += sizeof(double)){
        memcpy(&(table[*idx_start]), &data[i], sizeof(double));
        (*idx_start)++;
    }
    return EMU_OK;
}

/* ============================================================================
    EXECUTION LOGIC
   ============================================================================ */

emu_result_t block_math(block_handle_t* block){
    emu_result_t res = {.code = EMU_OK};
    
    if (!emu_block_check_inputs_updated(block)) {EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");}

    emu_variable_t var;
    bool EN = true; 
    if (block->inputs[0]){
        var = mem_get(block->inputs[0], 0);
        if (likely(var.error == EMU_OK)) {
            EN = (bool)emu_var_to_double(var);
            if(!EN){EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");}
        }else{EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");}
    }
    

    expression_t* eval = (expression_t*)block->custom_data;
    double stack[16];
    int over_top = 0;
    double result = 0;

    for(uint16_t i = 0; i < eval->count; i++){
        instruction_t *ins = &(eval->code[i]);
        switch(ins->op){
            case OP_VAR: {
                emu_variable_t v = mem_get(block->inputs[ins->input_index], false);
                double tmp = emu_var_to_double(v);
                if(over_top < 16) stack[over_top++] = tmp;
                break;
            }
            case OP_CONST:
                if(over_top < 16) stack[over_top++] = eval->constant_table[ins->input_index];
                break;
            case OP_ADD:
                if(over_top >= 2) { stack[over_top-2] += stack[over_top-1]; over_top--; }
                break;
            case OP_MUL:
                if(over_top >= 2) { stack[over_top-2] *= stack[over_top-1]; over_top--; }
                break;
            case OP_DIV:
                if(over_top >= 2) {
                    if(is_zero(stack[over_top-1])){EMU_RETURN_NOTICE(EMU_ERR_BLOCK_DIV_BY_ZERO, block->cfg.block_idx, TAG, "Div by zero");}
                    stack[over_top-2] /= stack[over_top-1];
                    over_top--;
                }
                break;
            case OP_COS:
                if(over_top >= 1) stack[over_top-1] = cos(stack[over_top-1]);
                break;
            case OP_SIN:
                if(over_top >= 1) stack[over_top-1] = sin(stack[over_top-1]);
                break; 
            case OP_POW:
                if(over_top >= 2) { stack[over_top-2] = pow(stack[over_top-2], stack[over_top-1]); over_top--; }
                break;
            case OP_ROOT:
                if(over_top >= 1) stack[over_top-1] = sqrt(stack[over_top-1]);
                break;
            case OP_SUB:
                if(over_top >= 2) { stack[over_top-2] -= stack[over_top-1]; over_top--; }
                break; 
        }
    }

    result = (over_top > 0) ? stack[0] : 0.0;
    
    // Set Outputs
    emu_variable_t v_eno = { .type = DATA_B, .data.b = true };
    res =emu_block_set_output(block, &v_eno, 0);
    LOG_I(TAG, "[%d]result %lf", block->cfg.block_idx, result);
    emu_variable_t v_res = { .type = DATA_D, .data.d = result };
    res = emu_block_set_output(block, &v_res, 1);
    if (unlikely(res.code != EMU_OK)) {EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output acces error %s", EMU_ERR_TO_STR(res.code));}
    return res;      
} 

/* ============================================================================
    HELPERS
   ============================================================================ */

static inline bool is_equal(double a, double b){
    return fabs(a-b) < DBL_EPSILON;
}

static inline bool is_zero(double a){
    return fabs(a) < DBL_EPSILON;
}

static inline bool is_greater(double a, double b){
    return (a - b) > DBL_EPSILON;
}

emu_err_t _clear_expression_internals(expression_t* expr){
    if(expr->code) free(expr->code);
    if(expr->constant_table) free(expr->constant_table);
    return EMU_OK;
}

void block_math_free(block_handle_t* block){
    if(block && block->custom_data){
        expression_t* expr = (expression_t*)block->custom_data;
        _clear_expression_internals(expr);
        free(expr);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared math block memory");
    }
}
emu_result_t block_math_verify(block_handle_t *block) {
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_MATH", "Custom Data is NULL");}

    expression_t *data = (expression_t*)block->custom_data;

    if (data->count == 0) {EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_MATH", "Empty expression (count=0)");}

    if (data->count > 0 && data->code == NULL) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_MATH", "Code pointer is NULL");}
    return EMU_RESULT_OK();
}