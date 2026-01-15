#include "block_for.h"
#include "emulator_blocks.h"
#include "emulator_variables_acces.h"
#include "blocks_functions_list.h"
#include "emulator_loop.h" 
#include "emulator_body.h"
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
    double start_val;
    double end_val;
    double op_step;
    block_for_condition_t condition;
    block_for_operator_t op;
}block_for_handle_t;   


#define BLOCK_FOR_IN_EN    0
#define BLOCK_FOR_IN_START 1
#define BLOCK_FOR_IN_STOP  2
#define BLOCK_FOR_IN_STEP  3
#define BLOCK_FOR_MAX_CYCLES 1000


static emu_code_handle_t code;
static bool binded_to_code = false;

extern uint64_t emu_loop_iterator;

// Helper: safe double equality
static inline bool dbl_eq(double a, double b) {
    return fabs(a - b) < DBL_EPSILON;
}

/* ============================================================================
    EXECUTION LOGIC
   ============================================================================ */

emu_result_t block_for(block_handle_t *src) {
    emu_result_t res = {.code = EMU_OK};
    (void)res; // May be used by macros
    

    bool EN = block_check_EN(src, 0);
    
    if (!EN) { EMU_RETURN_OK(EMU_LOG_block_inactive, EMU_OWNER_block_for, src->cfg.block_idx, TAG, "Block Disabled"); }
    block_for_handle_t* config = (block_for_handle_t*)src->custom_data;
    if (!config) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_for, src->cfg.block_idx, 0, TAG, "No custom data");

    // 3. Pobieranie parametrów (Start / Stop / Step)
    double iterator_start = config->start_val;
    if (block_in_updated(src, BLOCK_FOR_IN_START)){MEM_GET(&iterator_start, src->inputs[BLOCK_FOR_IN_START]);}

    double limit = config->end_val;
    if (block_in_updated(src, BLOCK_FOR_IN_STOP)){MEM_GET(&limit, src->inputs[BLOCK_FOR_IN_STOP]);}


    double step = config->op_step;
    if (block_in_updated(src, BLOCK_FOR_IN_STEP)){MEM_GET(&step, src->inputs[BLOCK_FOR_IN_STEP]);}

    // 4. Inicjalizacja Pętli
    double current_val = iterator_start;
    uint32_t iteration = 0; 


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
        res = block_set_output(src, &v_en, 0);
        if (res.code != EMU_OK) EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_for, src->cfg.block_idx, 0, TAG, "Set Out 0 fail");

        emu_variable_t v_iter = { .type = DATA_D, .data.d = current_val };
        res = block_set_output(src, &v_iter, 1);
        if (res.code != EMU_OK) EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_for, src->cfg.block_idx, 0, TAG, "Set Out 1 fail");

        // --- E. WYKONANIE DZIECI (CHILD BLOCKS) ---
        for (uint16_t b = 1; b <= config->chain_len; b++) {
            block_handle_t* child = code->blocks_list[src->cfg.block_idx + b];
            if (likely(child)) {
                    if (unlikely(emu_loop_wtd_status())) {
                        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_FOR_TIMEOUT, EMU_OWNER_block_for, src->cfg.block_idx, 0, TAG, "WTD triggered, on block %d, elapsed time %lld, iteration %ld ,wtd set to %lld ms", src->cfg.block_idx + b-1, emu_loop_get_time(), iteration, emu_loop_get_wtd_max_skipped()*emu_loop_get_period()/1000);
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
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_for_parse, 0, 0, TAG, "NULL block");
    if(!binded_to_code){
        code = emu_get_current_code_ctx();
        binded_to_code = true;
    }
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
                    if (!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_for_parse, block_idx, 0, TAG, "Alloc failed");
                }
                
                block_for_handle_t* cfg = (block_for_handle_t*)block->custom_data;
                uint8_t packet_type = data[2];

                // Dispatch based on Packet Type
                if (packet_type == EMU_H_BLOCK_PACKET_CONST) {
                    // Packet 1: Doubles
                    if (_emu_parse_for_constants(data, len, cfg) != EMU_OK) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, EMU_OWNER_block_for_parse, block_idx, 0, TAG, "CONST parse failed");
                    }
                } 
                else if (packet_type == EMU_H_BLOCK_PACKET_CUSTOM) {
                    // Packet 2: Settings
                    if (_emu_parse_for_settings(data, len, cfg) != EMU_OK) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, EMU_OWNER_block_for_parse, block_idx, 0, TAG, "SETTINGS parse failed");
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
    if (!block->custom_data) { EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_for_verify, block->cfg.block_idx, 0, TAG, "Custom Data is NULL");}
    block_for_handle_t *data = (block_for_handle_t*)block->custom_data;

    if (data->condition > FOR_COND_LTE) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_for_verify, block->cfg.block_idx, 0, TAG, "Invalid Condition Enum: %d", data->condition);}
    if (data->op > FOR_OP_DIV) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_for_verify, block->cfg.block_idx, 0, TAG, "Invalid Op Enum: %d", data->op);}
    if (fabs(data->op_step) < 0.000001) {EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_for_verify, block->cfg.block_idx, 0, TAG, "Step is 0 (Infinite Loop risk)");}
    return EMU_RESULT_OK();
}