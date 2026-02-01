#include "block_math.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks.h" 
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = __FILE_NAME__;

// Internal Helpers
static __always_inline bool is_greater(float a, float b);
static __always_inline bool is_equal(float a, float b);
static __always_inline bool is_zero(float a);

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
    uint8_t input_index; //for operator it's 00, for constant index in constant table
} instruction_t;


typedef struct{
    instruction_t *code;
    uint8_t count;
    float *constant_table;
    uint8_t const_count;
} expression_t;


/* ============================================================================
    PARSING LOGIC - New packet-based approach
    Packet format: [block_idx:u16][block_type:u8][packet_id:u8][data...]
   ============================================================================ */

/**
 * @brief Parse constants packet
 * Format: [count:u8][float0][float1]...
 */
static emu_err_t _parse_constants(const uint8_t *data, uint16_t len, expression_t *expr) {
    if (len < 1) return EMU_ERR_PACKET_INCOMPLETE;
    
    uint8_t count = data[0];
    if (len < 1 + count * sizeof(float)) return EMU_ERR_PACKET_INCOMPLETE;
    
    if (expr->constant_table) free(expr->constant_table);
    expr->constant_table = (float*)calloc(count, sizeof(float));
    if (!expr->constant_table) return EMU_ERR_NO_MEM;
    
    expr->const_count = count;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(&expr->constant_table[i], &data[1 + i * sizeof(float)], sizeof(float));
    }
    
    LOG_I(TAG, "Parsed %d constants", count);
    return EMU_OK;
}

/**
 * @brief Parse instructions packet
 * Format: [count:u8][op0:u8][idx0:u8][op1:u8][idx1:u8]...
 */
static emu_err_t _parse_instructions(const uint8_t *data, uint16_t len, expression_t *expr) {
    if (len < 1) return EMU_ERR_PACKET_INCOMPLETE;
    
    uint8_t count = data[0];
    if (len < 1 + count * 2) return EMU_ERR_PACKET_INCOMPLETE;
    
    if (expr->code) free(expr->code);
    expr->code = (instruction_t*)calloc(count, sizeof(instruction_t));
    if (!expr->code) return EMU_ERR_NO_MEM;
    
    expr->count = count;
    for (uint8_t i = 0; i < count; i++) {
        expr->code[i].op = data[1 + i * 2];
        expr->code[i].input_index = data[1 + i * 2 + 1];
    }
    
    LOG_I(TAG, "Parsed %d instructions", count);
    return EMU_OK;
}

#undef OWNER
#define OWNER EMU_OWNER_block_math_parse
emu_result_t block_math_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr)
{
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "Null block ptr");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(expression_t));
        if (!block->custom_data) {
            RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "No memory for custom_data");
        }
    }
    
    expression_t *expr = (expression_t*)block->custom_data;
    emu_err_t err = EMU_OK;
    
    switch (packet_id) {
        case BLOCK_PKT_CONSTANTS:
            err = _parse_constants(payload, payload_len, expr);
            break;
        case BLOCK_PKT_INSTRUCTIONS:
            err = _parse_instructions(payload, payload_len, expr);
            break;
        default:
            LOG_W(TAG, "Unknown packet_id: 0x%02X", packet_id);
            break;
    }
    
    if (err != EMU_OK) {
        RET_ED(err, block->cfg.block_idx, 0, "Parse error for packet_id 0x%02X", packet_id);
    }
    
    return EMU_RESULT_OK();
}

/* ============================================================================
    EXECUTION LOGIC
   ============================================================================ */

#undef OWNER
#define OWNER EMU_OWNER_block_math
emu_result_t block_math(block_handle_t block){
    emu_result_t res = {.code = EMU_OK};
    if (!emu_block_check_inputs_updated(block)) { RET_OKD(block->cfg.block_idx, "Block Disabled"); }
    if(!block_check_EN(block, 0)){ RET_OKD(block->cfg.block_idx, "Block Disabled"); }
    
    expression_t* eval = (expression_t*)block->custom_data;
    float stack[16];
    int over_top = 0;
    float result = 0.0f;
    float tmp = 0.0f;
    for(uint16_t i = 0; i < eval->count; i++){
        instruction_t *ins = &(eval->code[i]);
        switch(ins->op){
            case OP_VAR: {
                MEM_GET(&tmp, block->inputs[ins->input_index]);
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
                    if(is_zero(stack[over_top-1])){RET_ND(EMU_ERR_BLOCK_DIV_BY_ZERO, block->cfg.block_idx, 0, "Div by zero");}
                    stack[over_top-2] /= stack[over_top-1];
                    over_top--;
                }
                break;
            case OP_COS:
                if(over_top >= 1) stack[over_top-1] = cosf(stack[over_top-1]);
                break;
            case OP_SIN:
                if(over_top >= 1) stack[over_top-1] = sinf(stack[over_top-1]);
                break; 
            case OP_POW:
                if(over_top >= 2) { stack[over_top-2] = powf(stack[over_top-2], stack[over_top-1]); over_top--; }
                break;
            case OP_ROOT:
                if(over_top >= 1) stack[over_top-1] = sqrtf(stack[over_top-1]);
                break;
            case OP_SUB:
                if(over_top >= 2) { stack[over_top-2] -= stack[over_top-1]; over_top--; }
                break; 
        }
    }

    result = (over_top > 0) ? stack[0] : 0.0f;
    
    // Set Outputs
    mem_var_t v_eno = { .type = MEM_B, .data.val.b = true };
    res = block_set_output(block, v_eno, 0);
    LOG_I(TAG, "[%d]result %f", block->cfg.block_idx, result);
    mem_var_t v_res = { .type = MEM_F, .data.val.f = result };
    res = block_set_output(block, v_res, 1);
    if (unlikely(res.code != EMU_OK)) {RET_ED(res.code, block->cfg.block_idx, 0, "Output acces error %s", EMU_ERR_TO_STR(res.code));}
    RET_OKD(block->cfg.block_idx, "[%d]result: %f", block->cfg.block_idx, result);     
} 

/* ============================================================================
    HELPERS
   ============================================================================ */

static __always_inline bool is_equal(float a, float b){
    return fabsf(a-b) < FLT_EPSILON;
}
    
static __always_inline bool is_zero(float a){
    return fabsf(a) < FLT_EPSILON;
}

static __always_inline bool is_greater(float a, float b){
    return (a - b) > FLT_EPSILON;
}

emu_err_t _clear_expression_internals(expression_t* expr){
    if(expr->code) free(expr->code);
    if(expr->constant_table) free(expr->constant_table);
    return EMU_OK;
}

void block_math_free(block_handle_t block){
    if(block && block->custom_data){
        expression_t* expr = (expression_t*)block->custom_data;
        _clear_expression_internals(expr);
        free(expr);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared math block memory");
    }
}
#undef OWNER
#define OWNER EMU_OWNER_block_math_verify
emu_result_t block_math_verify(block_handle_t block) {
    if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL");}

    expression_t *data = (expression_t*)block->custom_data;

    if (data->count == 0) {RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Empty expression (count=0)");}

    if (data->count > 0 && data->code == NULL) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Code pointer is NULL");}
    return EMU_RESULT_OK();
}