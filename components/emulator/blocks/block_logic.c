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
    float *constant_table;
} logic_expression_t;

// Internal Helpers
static __always_inline bool is_true(float a) {return a > 0.5f;}
static __always_inline float bool_to_float(bool val) {return val ? 1.0f : 0.0f;}

#undef OWNER
#define OWNER EMU_OWNER_block_logic
emu_result_t block_logic(block_handle_t block) {
    //necessary checks for block execution
    if (!emu_block_check_inputs_updated(block)) {REP_OKD(block->cfg.block_idx, "Block logic %d inactive (EN not updated)", block->cfg.block_idx);}
    if(!block_check_in_true(block, 0)){REP_OKD(block->cfg.block_idx, "Block logic %d inactive (EN not enabled)", block->cfg.block_idx);}

    //stack opertatin variables
    logic_expression_t* eval = (logic_expression_t*)block->custom_data;
    float stack[16]; 
    int8_t over_top = 0;
    float val_a, val_b; 
    emu_result_t res;

    //this part gets all inputs first so we don't have to access memory multiple times during eval
    float inputs[block->cfg.in_cnt];
    //skip input 0 (EN)
    for (uint8_t i = 1; i < block->cfg.in_cnt; i++) {
        if(block->cfg.in_connceted_mask & (1 << i)){
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
                    stack[over_top++] = bool_to_float(val_a > val_b); 
                }
                break;

            case CMP_OP_LT:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(val_a < val_b);
                }
                break;

            case CMP_OP_EQ:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(fabsf(val_a - val_b) < FLT_EPSILON);
                }
                break;

            case CMP_OP_GTE:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(val_a >= val_b); 
                }
                break;

            case CMP_OP_LTE:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(val_a <= val_b);
                }
                break;    
            
            case CMP_OP_AND:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(is_true(val_a) && is_true(val_b));
                }
                break;

            case CMP_OP_OR:
                if (over_top >= 2) {
                    val_b = stack[--over_top];
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(is_true(val_a) || is_true(val_b));
                }
                break;

            case CMP_OP_NOT: 
                if (over_top >= 1) {
                    val_a = stack[--over_top];
                    stack[over_top++] = bool_to_float(!is_true(val_a));
                }
                break;

            default:
                RET_ED(EMU_ERR_INVALID_DATA, block->cfg.block_idx, 0, "Invalid instruction: %d", ins->op);
        }
    }

    //get final result
    bool final_bool = (over_top > 0) ? is_true(stack[0]) : false;
    
    //We set ENO and OUT
    mem_var_t v_out = { .type = MEM_B, .data.val.b = true};
    res = block_set_output(block, v_out, 0);
    v_out.data.val.b = final_bool;
    res = block_set_output(block, v_out, 1);

    if (unlikely(res.code != EMU_OK)) {RET_ED(res.code, block->cfg.block_idx, ++res.depth, "Output acces error: %s", EMU_ERR_TO_STR(res.code));}
    REP_OKD(block->cfg.block_idx, "[%d]result: %s", block->cfg.block_idx, final_bool ? "TRUE" : "FALSE");
    return res;
}


/* ============================================================================
    PARSING LOGIC - New packet-based approach
    Packet format: [block_idx:u16][block_type:u8][packet_id:u8][data...]
   ============================================================================ */

/**
 * @brief Parse constants packet
 * Format: [count:u8][float0][float1]...
 */
static emu_err_t _parse_logic_constants(const uint8_t *data, uint16_t len, logic_expression_t *expr) {
    if (len < 1) return EMU_ERR_PACKET_INCOMPLETE;
    
    uint8_t count = data[0];
    if (len < 1 + count * sizeof(float)) return EMU_ERR_PACKET_INCOMPLETE;
    
    if (expr->constant_table) free(expr->constant_table);
    expr->constant_table = (float*)calloc(count, sizeof(float));
    if (!expr->constant_table) return EMU_ERR_NO_MEM;
    
    for (uint8_t i = 0; i < count; i++) {
        memcpy(&expr->constant_table[i], &data[1 + i * sizeof(float)], sizeof(float));
    }
    
    LOG_I(TAG, "Parsed %d logic constants", count);
    return EMU_OK;
}

/**
 * @brief Parse instructions packet
 * Format: [count:u8][op0:u8][idx0:u8][op1:u8][idx1:u8]...
 */
static emu_err_t _parse_logic_instructions(const uint8_t *data, uint16_t len, logic_expression_t *expr) {
    if (len < 1) return EMU_ERR_PACKET_INCOMPLETE;
    
    uint8_t count = data[0];
    if (len < 1 + count * 2) return EMU_ERR_PACKET_INCOMPLETE;
    
    if (expr->code) free(expr->code);
    expr->code = (logic_instruction_t*)calloc(count, sizeof(logic_instruction_t));
    if (!expr->code) return EMU_ERR_NO_MEM;
    
    expr->count = count;
    for (uint8_t i = 0; i < count; i++) {
        expr->code[i].op = data[1 + i * 2];
        expr->code[i].input_index = data[1 + i * 2 + 1];
    }
    
    LOG_I(TAG, "Parsed %d logic instructions", count);
    return EMU_OK;
}

#undef OWNER
#define OWNER EMU_OWNER_block_logic_parse
emu_result_t block_logic_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr)
{
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block pointer");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(logic_expression_t));
        if (!block->custom_data) {
            RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "No memory for logic custom_data");
        }
    }
    
    logic_expression_t *expr = (logic_expression_t*)block->custom_data;
    emu_err_t err = EMU_OK;
    
    switch (packet_id) {
        case BLOCK_PKT_CONSTANTS:
            err = _parse_logic_constants(payload, payload_len, expr);
            break;
        case BLOCK_PKT_INSTRUCTIONS:
            err = _parse_logic_instructions(payload, payload_len, expr);
            break;
        default:
            LOG_W(TAG, "Unknown logic packet_id: 0x%02X", packet_id);
            break;
    }
    
    if (err != EMU_OK) {
        RET_ED(err, block->cfg.block_idx, 0, "Logic parse error for packet_id 0x%02X", packet_id);
    }
    
    return EMU_RESULT_OK();
}

static emu_err_t _clear_logic_expression_internals(logic_expression_t* expr){
    if(expr->code) free(expr->code);
    if(expr->constant_table) free(expr->constant_table);
    return EMU_OK;
}

void block_logic_free(block_handle_t block){
    if(block && block->custom_data){
        logic_expression_t* expr = (logic_expression_t*)block->custom_data;
        _clear_logic_expression_internals(expr);
        free(expr);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared logic block data");
    }
    return;
}

#undef OWNER
#define OWNER EMU_OWNER_block_logic_verify
emu_result_t block_logic_verify(block_handle_t block) {
    if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL %d", block->cfg.block_idx);}
    logic_expression_t *data = (logic_expression_t*)block->custom_data;
    if (data->count == 0) {RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Empty expression (count=0) %d", block->cfg.block_idx);}

    if (data->count > 0 && data->code == NULL) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Code pointer is NULL, %d", block->cfg.block_idx);}

    return EMU_RESULT_OK();
}