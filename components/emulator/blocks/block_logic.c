#include "block_logic.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h" 
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = __FILE_NAME__;

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

// Internal Helpers
static __always_inline bool is_true(double a) {return a > 0.5;}
static __always_inline double bool_to_double(bool val) {return val ? 1.0 : 0.0;}

emu_result_t block_logic(block_handle_t* block) {
    //necessary checks for block execution
    if (!emu_block_check_inputs_updated(block)) {EMU_REPORT(EMU_LOG_block_inactive, EMU_OWNER_block_logic, block->cfg.block_idx, TAG, "Block logic %d inactive (EN not updated)", block->cfg.block_idx);}
    if(!block_check_EN(block, 0)){EMU_REPORT(EMU_LOG_block_inactive, EMU_OWNER_block_logic, block->cfg.block_idx, TAG, "Block logic %d inactive (EN not enabled)", block->cfg.block_idx);}

    //stack opertatin variables
    logic_expression_t* eval = (logic_expression_t*)block->custom_data;
    double stack[16]; 
    int8_t over_top = 0;
    double val_a, val_b; 
    emu_result_t res;

    //this part gets all inputs first so we don't have to access memory multiple times during eval
    double inputs[block->cfg.in_cnt];
    //skip input 0 (EN)
    for (uint8_t i = 1; i < block->cfg.in_cnt; i++) {
        if(block->cfg.in_connected & (1 << i)){
            MEM_GET(&inputs[i], block->inputs[i]);
        }
    }

    //execute all instructions for comparisons
    for (int8_t i = 0; i < eval->count; i++) {
        logic_instruction_t *ins = &(eval->code[i]);
          
        switch (ins->op) {
            case CMP_OP_VAR: 
                stack[over_top++] = inputs[ins->input_index];
                break;

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
                EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, EMU_OWNER_block_logic, block->cfg.block_idx, 0, TAG, "Invalid instruction: %d", ins->op);
        }
    }

    //get final result
    bool final_bool = (over_top > 0) ? is_true(stack[0]) : false;
    
    //We set ENO and OUT
    emu_variable_t v_out = { .type = DATA_B, .data.b = true};
    res = block_set_output(block, &v_out, 0);
    v_out.data.b = final_bool;
    res = block_set_output(block, &v_out, 1);

    if (unlikely(res.code != EMU_OK)) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_logic, block->cfg.block_idx, ++res.depth, TAG, "Output acces error: %s", EMU_ERR_TO_STR(res.code));}
    EMU_REPORT(EMU_LOG_finished, EMU_OWNER_block_logic, block->cfg.block_idx, TAG, "[%d]result: %s", block->cfg.block_idx, final_bool ? "TRUE" : "FALSE");
    return res;
}




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
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_logic_parse, 0, 0, TAG, "NULL block pointer");

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
                        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_logic_parse, block_idx, 0, TAG, "No memory for logic custom_data");
                    }
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_logic_expr(source, search_idx, (logic_expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                    block_logic_free(block);
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_logic_parse, block_idx, 0, TAG, "Logic Expr parse error");
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
                     if(!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_logic_parse, block_idx, 0, TAG, "No memory for logic custom_data");
                }

                size_t const_msg_cnt = 1;
                if(_emu_parse_logic_const(source, search_idx, (logic_expression_t*)(block->custom_data), &const_msg_cnt) != EMU_OK){
                     block_logic_free(block);
                     EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_logic_parse, block_idx, 0, TAG, "Error parsing constants");
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
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_logic_verify, block->cfg.block_idx, 0, TAG, "Custom Data is NULL %d", block->cfg.block_idx);}
    logic_expression_t *data = (logic_expression_t*)block->custom_data;
    if (data->count == 0) {EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_logic_verify, block->cfg.block_idx, 0, TAG, "Empty expression (count=0) %d", block->cfg.block_idx);}

    if (data->count > 0 && data->code == NULL) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_logic_verify, block->cfg.block_idx, 0, TAG, "Code pointer is NULL, %d", block->cfg.block_idx);}

    return EMU_RESULT_OK();
}