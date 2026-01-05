/*
 * components.zip/emulator/blocks/block_for.c
 */

#include "block_for.h"
#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_loop.h" 
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "BLOCK_FOR";

extern block_handle_t **emu_block_struct_execution_list; 
extern uint16_t emu_loop_iterator;
extern emu_block_func blocks_main_functions_table[255];
extern emu_loop_handle_t loop_handle; 

#define BLOCK_FOR_IN_START 1
#define BLOCK_FOR_IN_STOP  2
#define BLOCK_FOR_IN_STEP  3

// Helper: safe double equality
static inline bool dbl_eq(double a, double b) {
    return fabs(a - b) < DBL_EPSILON;
}

/* ============================================================================
    EXECUTION LOGIC
   ============================================================================ */

emu_result_t block_for(block_handle_t *src) {
    emu_result_t res = {.code = EMU_OK};
    emu_variable_t var;
    
    // 1. Walidacja wejść (Status)
    if (!emu_block_check_inputs_updated(src)) {
        EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, src->cfg.block_idx, TAG, "Block is inactive (Inputs not ready)");
    }

    // 2. Wejście EN (Input 0)
    bool EN = true;
    var = mem_get(src->inputs[0], false);
    if (var.error == EMU_OK) {
        EN = (emu_var_to_double(var) > 0.5);
    } else {
        EN = false;
    }
    
    if (!EN) {
        EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, src->cfg.block_idx, TAG, "Block Disabled (EN=0)");
    }

    block_for_handle_t* config = (block_for_handle_t*)src->custom_data;
    if (!config) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, src->cfg.block_idx, TAG, "No custom data");

    // 3. Pobieranie parametrów (Start / Stop / Step)
    double iterator_start = config->start_val;
    if (src->cfg.in_cnt > BLOCK_FOR_IN_START && src->inputs[BLOCK_FOR_IN_START]) {
        var = mem_get(src->inputs[BLOCK_FOR_IN_START], false);
        if (var.error == EMU_OK) iterator_start = emu_var_to_double(var);
    }

    double limit = config->end_val;
    if (src->cfg.in_cnt > BLOCK_FOR_IN_STOP && src->inputs[BLOCK_FOR_IN_STOP]) {
        var = mem_get(src->inputs[BLOCK_FOR_IN_STOP], false);
        if (var.error == EMU_OK) limit = emu_var_to_double(var);
    }

    double step = config->op_step;
    if (src->cfg.in_cnt > BLOCK_FOR_IN_STEP && src->inputs[BLOCK_FOR_IN_STEP]) {
        var = mem_get(src->inputs[BLOCK_FOR_IN_STEP], false);
        if (var.error == EMU_OK) step = emu_var_to_double(var);
    }

    // 4. Inicjalizacja Pętli
    double current_val = iterator_start;
    uint32_t iteration = 0; 

    emu_loop_ctx_t *ctx = (emu_loop_ctx_t *)loop_handle;

    while(1) {
        bool condition_met = false;


        // --- B. WARUNEK LOGICZNY PĘTLI ---
        switch (config->condition) {
            case FOR_COND_GT:  condition_met = (current_val > (limit + DBL_EPSILON)); break;
            case FOR_COND_LT:  condition_met = (current_val < (limit - DBL_EPSILON)); break;
            case FOR_COND_GTE: condition_met = (current_val >= (limit - DBL_EPSILON)); break;
            case FOR_COND_LTE: condition_met = (current_val <= (limit + DBL_EPSILON)); break;
            default: break;
        }

        if (!condition_met) break;


        // --- D. WYJŚCIA ---
        emu_variable_t v_en = { .type = DATA_B, .data.b = true };
        res = emu_block_set_output(src, &v_en, 0);
        if (res.code != EMU_OK) EMU_RETURN_CRITICAL(res.code, src->cfg.block_idx, TAG, "Set Out 0 fail");

        emu_variable_t v_iter = { .type = DATA_D, .data.d = current_val };
        res = emu_block_set_output(src, &v_iter, 1);
        if (res.code != EMU_OK) EMU_RETURN_CRITICAL(res.code, src->cfg.block_idx, TAG, "Set Out 1 fail");

        // --- E. WYKONANIE DZIECI (CHILD BLOCKS) ---
        for (uint16_t b = 1; b <= config->chain_len; b++) {
            block_handle_t* child = emu_block_struct_execution_list[src->cfg.block_idx + b];
            if (likely(child)) {
                if (likely(ctx)) {
                    if (unlikely(((volatile emu_loop_ctx_t*)ctx)->wtd.wtd_triggered )) {
                        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_FOR_TIMEOUT, src->cfg.block_idx, TAG, "WTD triggered, on block %d, elapsed time %lld, iteration %ld ,wtd set to %lld ms", src->cfg.block_idx + b-1, ctx->timer.time, iteration, ctx->wtd.max_skipp*ctx->timer.loop_period/1000);
                    }
                }
                emu_block_reset_outputs_status(child);
                
                uint8_t child_type = child->cfg.block_type;
                if (child_type < 255) {
                    emu_block_func child_func = blocks_main_functions_table[child_type];
                    if (child_func) {
                        res = child_func(child);
                        if (res.code != EMU_OK && res.code != EMU_ERR_BLOCK_INACTIVE) {
                            return res; 
                        }
                    }
                }
            }
        }

        // --- F. KROK (STEP) ---
        switch(config->op) {
            case FOR_OP_ADD: current_val += step; break;
            case FOR_OP_SUB: current_val -= step; break;
            case FOR_OP_MUL: current_val *= step; break;
            case FOR_OP_DIV: 
                if (fabs(step) > DBL_EPSILON) current_val /= step; 
                break;
        }
        iteration++;
    }

    // Przesunięcie globalnego iteratora o długość łańcucha, aby pominąć dzieci w głównej pętli execution_list
    emu_loop_iterator += config->chain_len;
    
    return EMU_RESULT_OK();
}

/* ============================================================================
    PARSING LOGIC
   ============================================================================ */

/**
 * @brief Parses the CONSTants packet (start, limit, step)
 * Python: struct.pack('<ddd', self.config_start, self.config_limit, self.config_step)
 */
static emu_err_t _emu_parse_for_constants(uint8_t *data, uint16_t len, block_for_handle_t* handle) {
    // Header (5 bytes) + 3 * 8 bytes (doubles) = 29 bytes
    if (len < 29) return EMU_ERR_INVALID_DATA; 

    size_t offset = 5; 
    
    // [8B] Start
    memcpy(&handle->start_val, &data[offset], 8); offset += 8;
    
    // [8B] End (Limit)
    memcpy(&handle->end_val, &data[offset], 8); offset += 8;
    
    // [8B] Step
    memcpy(&handle->op_step, &data[offset], 8); offset += 8;
    
    LOG_I(TAG, "Parsed CONST: Start=%.2f End=%.2f Step=%.2f", 
          handle->start_val, handle->end_val, handle->op_step);
          
    return EMU_OK;
}

/**
 * @brief Parses the CUSTOM config packet (chain_len, condition, operator)
 * Python: struct.pack('<HBB', self.chain_len, int(self.condition), int(self.operator))
 */
static emu_err_t _emu_parse_for_settings(uint8_t *data, uint16_t len, block_for_handle_t* handle) {
    // Header (5 bytes) + 2 (short) + 1 (byte) + 1 (byte) = 9 bytes minimum
    if (len < 9) return EMU_ERR_INVALID_DATA;

    size_t offset = 5;

    // [2B] Chain Length
    memcpy(&handle->chain_len, &data[offset], 2); offset += 2;

    // [1B] Condition
    handle->condition = (block_for_condition_t)data[offset++];

    // [1B] Operator
    handle->op = (block_for_operator_t)data[offset++];

    LOG_I(TAG, "Parsed SETTINGS: Chain=%d Cond=%d Op=%d", 
          handle->chain_len, handle->condition, handle->op);

    return EMU_OK;
}

emu_result_t block_for_parse(chr_msg_buffer_t *source, block_handle_t *block) {
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "NULL block");

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Common Header Check: [0xBB][BLOCK_TYPE][PACKET_TYPE][ID_L][ID_H]
        if (len > 5 && data[0] == 0xBB && data[1] == BLOCK_FOR) {
            uint16_t block_idx = 0;
            memcpy(&block_idx, &data[3], 2);
            
            if (block_idx == block->cfg.block_idx) {
                // Ensure memory is allocated
                if (block->custom_data == NULL) {
                    block->custom_data = calloc(1, sizeof(block_for_handle_t));
                    if (!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Alloc failed");
                }
                
                block_for_handle_t* cfg = (block_for_handle_t*)block->custom_data;
                uint8_t packet_type = data[2];

                // Dispatch based on Packet Type
                if (packet_type == EMU_H_BLOCK_PACKET_CONST) {
                    // Packet 1: Doubles
                    if (_emu_parse_for_constants(data, len, cfg) != EMU_OK) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, block_idx, TAG, "CONST parse failed");
                    }
                } 
                else if (packet_type == EMU_H_BLOCK_PACKET_CUSTOM) {
                    // Packet 2: Settings
                    if (_emu_parse_for_settings(data, len, cfg) != EMU_OK) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, block_idx, TAG, "SETTINGS parse failed");
                    }
                }
            }
        }
    }
    return EMU_RESULT_OK();
}

void block_for_free(block_handle_t* block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared for block data");
    }
    return;
}
emu_result_t block_for_verify(block_handle_t *block) {
    if (!block->custom_data) { EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_FOR", "Custom Data is NULL");}
    block_for_handle_t *data = (block_for_handle_t*)block->custom_data;

    if (data->condition > FOR_COND_LTE) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_FOR", "Invalid Condition Enum: %d", data->condition);}
    if (data->op > FOR_OP_DIV) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_FOR", "Invalid Op Enum: %d", data->op);}
    if (fabs(data->op_step) < 0.000001) {EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_FOR", "Step is 0 (Infinite Loop risk)");}
    return EMU_RESULT_OK();
}