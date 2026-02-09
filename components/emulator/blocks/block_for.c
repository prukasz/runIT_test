#include "block_for.h"
#include "emu_blocks.h"
#include "emu_variables_acces.h"
#include "blocks_functions_list.h"
#include "emu_loop.h" 
#include "emu_body.h"
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = __FILE_NAME__;

typedef enum{
    FOR_COND_GT       = 0x01, // >
    FOR_COND_LT       = 0x02, // <
    FOR_COND_GTE      = 0x04, // >=
    FOR_COND_LTE      = 0x05, // <=
}block_for_condition_t;

typedef enum{
    FOR_OP_ADD        = 0x01,
    FOR_OP_SUB        = 0x02,
    FOR_OP_MUL        = 0x03,
    FOR_OP_DIV        = 0x04,
}block_for_operator_t;

 
typedef struct{
    uint16_t chain_len;
    float start_val;
    float end_val;
    float op_step;
    block_for_condition_t condition;
    block_for_operator_t op;
    // Cached runtime values
    float cached_start;
    float cached_end;
    float cached_step;
    float cached_limit_adjusted; // limit ± epsilon pre-calculated
    bool has_dynamic_inputs;
    bool first_run; // flag to ensure initialization
}block_for_handle_t;   


#define BLOCK_FOR_IN_EN    0
#define BLOCK_FOR_IN_START 1
#define BLOCK_FOR_IN_STOP  2
#define BLOCK_FOR_IN_STEP  3
#define BLOCK_FOR_MAX_CYCLES 1000


static emu_code_handle_t code;
static bool binded_to_code = false;

extern uint64_t emu_loop_iterator;

// Helper: safe float equality
static inline bool flt_eq(float a, float b) {
    return fabsf(a - b) < FLT_EPSILON;
}

/*-------------------------------BLOCK IMPLEMENTATION---------------------------------------------- */

#undef OWNER
#define OWNER EMU_OWNER_block_for
emu_result_t block_for(block_handle_t block) {
    emu_result_t res = EMU_RESULT_OK();
    
    bool EN = block_check_in_true(block, 0);
    
    if (!EN) {RET_OK_INACTIVE(block->cfg.block_idx);}

    block_for_handle_t* config = (block_for_handle_t*)block->custom_data;
    

    // 3. Update cached parameters only if inputs changed
    if (block_in_updated(block, BLOCK_FOR_IN_START)) {MEM_GET(&config->cached_start, block->inputs[BLOCK_FOR_IN_START]);}
    
    bool limit_changed = false;

    if (block_in_updated(block, BLOCK_FOR_IN_STOP)) {
        MEM_GET(&config->cached_end, block->inputs[BLOCK_FOR_IN_STOP]);
        limit_changed = true;
    }

    if (block_in_updated(block, BLOCK_FOR_IN_STEP)) {
        MEM_GET(&config->cached_step, block->inputs[BLOCK_FOR_IN_STEP]);
    }

    // Calculate limit adjustment on first run or when limit changes
    if (config->first_run || limit_changed) {
        switch (config->condition) {
            case FOR_COND_GT:  config->cached_limit_adjusted = config->cached_end + FLT_EPSILON; break;
            case FOR_COND_LT:  config->cached_limit_adjusted = config->cached_end - FLT_EPSILON; break;
            case FOR_COND_GTE: config->cached_limit_adjusted = config->cached_end - FLT_EPSILON; break;
            case FOR_COND_LTE: config->cached_limit_adjusted = config->cached_end + FLT_EPSILON; break;
        }
        config->first_run = false;
    }

    // Use cached values
    float current_val = config->cached_start;
    float limit_adj = config->cached_limit_adjusted;
    float step = config->cached_step;
    uint32_t iteration = 0;

    // Write EN output once (always true during loop)
    mem_var_t v_en = { .type = MEM_B, .data.val.b = true };
    res = block_set_output(block, v_en, 0);
    if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, 0, "Set Out 0 fail");

    while(1) {
        
        bool condition_met;
        switch (config->condition) {
            case FOR_COND_GT:  condition_met = (current_val > limit_adj); break;
            case FOR_COND_LT:  condition_met = (current_val < limit_adj); break;
            case FOR_COND_GTE: condition_met = (current_val >= limit_adj); break;
            case FOR_COND_LTE: condition_met = (current_val <= limit_adj); break;
            default: condition_met = false; break;
        }

        if (unlikely(!condition_met)) break;

        // update iteration output
        mem_var_t v_iter = { .type = MEM_F, .data.val.f = current_val };
        res = block_set_output(block, v_iter, 1);
        if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, 0, "[%"PRIu16"] Set Out 1 fail", block->cfg.block_idx);

        if (unlikely(emu_loop_wtd_status())) {
            RET_ED(EMU_ERR_BLOCK_FOR_TIMEOUT, block->cfg.block_idx, 0, "WTD triggered, elapsed time %"PRIu64", iteration %"PRIu32", wtd set to %"PRIu64" ms", emu_loop_get_time(), iteration, emu_loop_get_wtd_max_skipped()*emu_loop_get_period()/1000);
        }

        for (uint16_t b = 1; b <= config->chain_len; b++) {
            block_handle_t child = code->blocks_list[block->cfg.block_idx + b];
            if (likely(child)) {
                emu_block_reset_outputs_status(child);
                
                uint8_t child_type = child->cfg.block_type;
                if (likely(child_type < 255)) {
                    emu_block_func child_func = blocks_main_functions_table[child_type];
                    if (likely(child_func)) {
                        res = child_func(child);
                        if (unlikely(res.code != EMU_OK && res.code != EMU_ERR_BLOCK_INACTIVE)) {
                            return res; 
                        }
                    }
                }
            }
        }

        if (likely(config->op == FOR_OP_ADD)) {
            current_val += step;
        } else {
            switch(config->op) {
                case FOR_OP_SUB: current_val -= step; break;
                case FOR_OP_MUL: current_val *= step; break;
                case FOR_OP_DIV: 
                    if (likely(fabsf(step) > FLT_EPSILON)) current_val /= step; 
                    break;
                default: break;
            }
        }
        iteration++;
    }

    emu_loop_iterator += config->chain_len;
    
    return EMU_RESULT_OK();
}

/*--------------------PARSE FUNCTION------------------------------------------------------------ */

#undef OWNER
#define OWNER EMU_OWNER_block_for_parse
emu_result_t block_for_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");
    
    if (!binded_to_code) {
        code = emu_get_current_code_ctx();
        binded_to_code = true;
    }
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(block_for_handle_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "Alloc failed");
    }
    
    block_for_handle_t *cfg = (block_for_handle_t*)block->custom_data;
    
    switch (packet_id) {
        case BLOCK_PKT_CONSTANTS:
            // Constants packet: [start:f32][end:f32][step:f32]
            if (payload_len < 12) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "CONST payload too short");
            
            memcpy(&cfg->start_val, &payload[0], 4);
            memcpy(&cfg->end_val,   &payload[4], 4);
            memcpy(&cfg->op_step,   &payload[8], 4);
            
            // Initialize cached values
            cfg->cached_start = cfg->start_val;
            cfg->cached_end = cfg->end_val;
            cfg->cached_step = cfg->op_step;
            
            LOG_I(TAG, "Parsed CONST: Start=%.2f End=%.2f Step=%.2f", 
                  cfg->start_val, cfg->end_val, cfg->op_step);
            break;
            
        case BLOCK_PKT_CFG:
            // Config packet: [chain_len:u16][condition:u8][operator:u8]
            if (payload_len < 4) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "CONFIG payload too short");
            
            memcpy(&cfg->chain_len, &payload[0], 2);
            cfg->condition = (block_for_condition_t)payload[2];
            cfg->op = (block_for_operator_t)payload[3];
            
            // Initialize cached values with defaults
            cfg->cached_start = cfg->start_val;
            cfg->cached_end = cfg->end_val;
            cfg->cached_step = cfg->op_step;
            cfg->has_dynamic_inputs = false;
            cfg->first_run = true;
            
            LOG_I(TAG, "Parsed CONFIG: Chain=%d Cond=%d Op=%d", 
                  cfg->chain_len, cfg->condition, cfg->op);
            break;
            
        default:
            LOG_W(TAG, "Unknown for block packet_id: 0x%02X", packet_id);
            break;
    }
    
    return EMU_RESULT_OK();
}

void block_for_free(block_handle_t block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared for block data");
    }
    return;
}
#undef OWNER
#define OWNER EMU_OWNER_block_for_verify
emu_result_t block_for_verify(block_handle_t block) {
    if (!block->custom_data) { RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL");}
    block_for_handle_t *data = (block_for_handle_t*)block->custom_data;

    if (data->condition > FOR_COND_LTE) {RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Invalid Condition Enum: %d", data->condition);}
    if (data->op > FOR_OP_DIV) {RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Invalid Op Enum: %d", data->op);}
    if (fabsf(data->op_step) < 0.000001f) {RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Step is 0 (Infinite Loop risk)");}
    return EMU_RESULT_OK();
}
