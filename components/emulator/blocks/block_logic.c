#include "block_logic.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h" 
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "BLOCK_LOGIC";

// Internal Helpers
static inline bool is_true(double a);
static inline double bool_to_double(bool val);

// Forward Declarations
static emu_err_t _emu_parse_logic_expr(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt);
static emu_err_t _emu_parse_logic_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, logic_instruction_t* code);
static emu_err_t _emu_parse_logic_const(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt);
static emu_err_t _emu_parse_logic_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table);
static emu_err_t _clear_logic_expression_internals(logic_expression_t* expr);

/* ============================================================================
    PARSING LOGIC
   ============================================================================ */

emu_result_t block_logic_parse(chr_msg_buffer_t *source, block_handle_t *block)
{
    // Użycie makra bez słowa 'return' (makro zawiera return)
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "NULL block pointer");

    emu_result_t res = EMU_RESULT_OK();
    uint8_t *data;
    size_t len;
    
    uint16_t block_idx = block->cfg.block_idx;
    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx = 0;

    // --- PASS 1: EXPRESSION CODE (Subtype 0x02) ---
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (len > 3 && data[0] == 0xBB && data[1] == BLOCK_LOGIC && data[2] == 0x02)
        {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);

            if (packet_blk_id == block_idx)
            {
                LOG_I(TAG, "Detected Logic Expression header for block %d", block_idx);
                
                if (!block->custom_data) {
                    block->custom_data = calloc(1, sizeof(logic_expression_t));
                    if(!block->custom_data){
                        // Makro loguje błąd i zwraca wynik
                        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "No memory for logic custom_data");
                    }
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_logic_expr(source, search_idx, (logic_expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                    block_logic_free(block);
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Logic Expr parse error");
                }
            }
        }
        search_idx++;
    }

    // --- PASS 2: CONSTANT TABLE (Subtype 0x01) ---
    search_idx = 0;
    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (len > 3 && data[0] == 0xBB && data[1] == BLOCK_LOGIC && data[2] == 0x01)
        {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);

            if (packet_blk_id == block_idx)
            {
                LOG_I(TAG, "Detected Constant Table for logic block %d", block_idx);
                
                if (!block->custom_data) {
                     block->custom_data = calloc(1, sizeof(logic_expression_t));
                     if(!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "No memory for logic custom_data");
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_logic_const(source, search_idx, (logic_expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                     block_logic_free(block);
                     EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Error parsing constants");
                }
            }
        }
        search_idx++;
    }
    return res;
}

static emu_err_t _emu_parse_logic_expr(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt)
{
    uint8_t *data;
    size_t len;
    uint8_t op_count = 0;
    uint8_t idx_start = 0;

    chr_msg_buffer_get(source, msg_index, &data, &len);
    if(len < 6) return EMU_ERR_PACKET_INCOMPLETE;

    op_count = data[5];
    expression->count = op_count;
    expression->code = (logic_instruction_t*)calloc(op_count, sizeof(logic_instruction_t));
    
    if(!expression->code){
        return EMU_ERR_NO_MEM;
    }
    
    size_t len_total = op_count * 2 + 1;
    _emu_parse_logic_expr_msg(data, len, 6, &idx_start, expression->code);

    while(len_total > (len - 5)){
        len_total = len_total - (len - 5);
        msg_index++;
        chr_msg_buffer_get(source, msg_index, &data, &len);
        if(!data) break;

        _emu_parse_logic_expr_msg(data, len, 5, &idx_start, expression->code);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_expr_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, logic_instruction_t* code)
{
    for(size_t i = start_index; i < len - 1; i += 2){
        code[*idx_start].op = data[i];
        code[*idx_start].input_index = data[i+1];
        (*idx_start)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_const(chr_msg_buffer_t *source, size_t msg_index, logic_expression_t* expression, size_t *const_msg_cnt){
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
        return EMU_ERR_NO_MEM;
    }

    _emu_parse_logic_const_msg(data, len, 6, &idx_start, expression->constant_table);

    while(len_total > (len - 5)){
        len_total = len_total - (len - 5);
        msg_index++;
        chr_msg_buffer_get(source, msg_index, &data, &len);
        if(!data) break;

        _emu_parse_logic_const_msg(data, len, 5, &idx_start, expression->constant_table);
        (*const_msg_cnt)++;
    }
    return EMU_OK;
}

static emu_err_t _emu_parse_logic_const_msg(uint8_t *data, uint16_t len, size_t start_index, uint8_t *idx_start, double* table){
    for(size_t i = start_index; i <= len - sizeof(double); i += sizeof(double)){
        memcpy(&(table[*idx_start]), &data[i], sizeof(double));
        (*idx_start)++;
    }
    return EMU_OK;
}

/* ============================================================================
    EXECUTION LOGIC
   ============================================================================ */

emu_result_t block_logic(block_handle_t* block) {
    // 1. Sprawdzenie aktywności (Notice)
    if (!emu_block_check_inputs_updated(block)) {
        EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");
    }

    logic_expression_t* eval = (logic_expression_t*)block->custom_data;
    double stack[16]; 
    int8_t over_top = 0;
    double val_a, val_b; 

    for (int8_t i = 0; i < eval->count; i++) {
        logic_instruction_t *ins = &(eval->code[i]);
          
        switch (ins->op) {
            case CMP_OP_VAR: {
                emu_variable_t var = mem_get(block->inputs[ins->input_index], false);
                if (likely(var.error == EMU_OK)) {
                    stack[over_top++] = emu_var_to_double(var);
                } else {
                    EMU_RETURN_CRITICAL(var.error, block->cfg.block_idx, TAG, "Input acces error: %s", EMU_ERR_TO_STR(var.error));
                }
                break;
            }

            case CMP_OP_CONST:
                stack[over_top++] = eval->constant_table[ins->input_index];
                break;
              
            case CMP_OP_GT:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(val_a > val_b); 
                }
                break;

            case CMP_OP_LT:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(val_a < val_b);
                }
                break;

            case CMP_OP_EQ:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(fabs(val_a - val_b) < DBL_EPSILON);
                }
                break;

            case CMP_OP_GTE:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(val_a >= val_b); 
                }
                break;

            case CMP_OP_LTE:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(val_a <= val_b);
                }
                break;    
            
            case CMP_OP_AND:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(is_true(val_a) && is_true(val_b));
                }
                break;

            case CMP_OP_OR:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(is_true(val_a) || is_true(val_b));
                }
                break;

            case CMP_OP_NOT: 
                if (over_top >= 1) {
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_double(!is_true(val_a));
                }
                break;

            default:
                EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, block->cfg.block_idx, TAG, "Invalid instruction: %d", ins->op);
        }
    }

    bool final_bool = (over_top > 0) ? is_true(stack[0]) : false;
    
    // Set Output 0 (Result)
    emu_variable_t v_out = { .type = DATA_B };
    v_out.data.b = true;
    emu_result_t res = emu_block_set_output(block, &v_out, 0);
    v_out.data.b = final_bool;
    res = emu_block_set_output(block, &v_out, 1);

    if (unlikely(res.code != EMU_OK)) {
        EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output acces error: %s", EMU_ERR_TO_STR(res.code));
    }
    LOG_I(TAG, "[%d]result: %s", block->cfg.block_idx, final_bool ? "TRUE" : "FALSE");
    return res;
}

/* Helper functions */
static inline bool is_true(double a) {
    return a > 0.5; 
}

static inline double bool_to_double(bool val) {
    return val ? 1.0 : 0.0;
}

static emu_err_t _clear_logic_expression_internals(logic_expression_t* expr){
    if(expr->code) free(expr->code);
    if(expr->constant_table) free(expr->constant_table);
    return EMU_OK;
}

void block_logic_free(block_handle_t* block){
    if(block && block->custom_data){
        logic_expression_t* expr = (logic_expression_t*)block->custom_data;
        _clear_logic_expression_internals(expr);
        free(expr);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared logic block data");
    }
    return;
}

emu_result_t block_logic_verify(block_handle_t *block) {
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_LOGIC", "Custom Data is NULL %d", block->cfg.block_idx);}
    logic_expression_t *data = (logic_expression_t*)block->custom_data;
    if (data->count == 0) {EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_LOGIC", "Empty expression (count=0) %d", block->cfg.block_idx);}

    if (data->count > 0 && data->code == NULL) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_LOGIC", "Code pointer is NULL, %d", block->cfg.block_idx);}

    return EMU_RESULT_OK();
}