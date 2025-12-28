#include "block_for.h"
#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "utils_global_access.h"
#include "utils_block_in_q_access.h"
#include "utils_parse.h"
#include "math.h"
#include "float.h"
#include "string.h"

static const char* TAG = "BLOCK_FOR";
#define START_SETTING_MASK  0b00000001
#define STOP_SETTING_MASK  0b00000010
#define STEP_SETTING_MASK   0b00000100

// Zmienne globalne emulatora
extern block_handle_t **emu_block_struct_execution_list; 
extern uint16_t emu_loop_iterator;

emu_result_t block_for(block_handle_t *src) {
    emu_result_t res = EMU_RESULT_OK();
    bool EN = false;
    double en_val = 0;

    if (utils_get_in_val_auto(src, 0, &en_val) == EMU_OK) {
        EN = (bool)en_val;
    } else {
        EN = true; 
    }
    if (!EN) {
        return res; 
    }
    block_for_handle_t* config = (block_for_handle_t*)src->extras;
    if (!config) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, src->block_idx); //do we leave it here?
    double temp_val = 0;
    double iterator_start = config->start_val;
    if(utils_get_in_val_auto(src, BLOCK_FOR_IN_START, &temp_val) == EMU_OK) {
        iterator_start = temp_val;
    }
    double limit = config->end_val;
    if (utils_get_in_val_auto(src, BLOCK_FOR_IN_STOP, &temp_val) == EMU_OK) {
        limit = temp_val;
    }
    double step = config->op_step;
    if (utils_get_in_val_auto(src, BLOCK_FOR_IN_STEP, &temp_val) == EMU_OK) {
        step = temp_val;
    }
    LOG_I(TAG, "[%d] For Loop: Start=%.2f, Limit=%.2f, Step=%.2f", src->block_idx, iterator_start, limit, step);
    double current_val = iterator_start;
    uint16_t watchdog = 0;
    while(1) {
        bool condition_met = false;
        
        // Sprawdzenie warunku
        switch (config->condition) {
            case FOR_COND_GT:  condition_met = (current_val > (limit + DBL_EPSILON)); break;
            case FOR_COND_LT:  condition_met = (current_val < (limit - DBL_EPSILON)); break;
            case FOR_COND_GTE: condition_met = (current_val >= (limit - DBL_EPSILON)); break;
            case FOR_COND_LTE: condition_met = (current_val <= (limit + DBL_EPSILON)); break;
            default: condition_met = false; break;
        }

        if (!condition_met) break;

        // Watchdog
        watchdog++;
        if (watchdog > BLOCK_FOR_MAX_CYCLES) {
            LOG_W(TAG, "[%d] Max cycles exceeded!", src->block_idx);
            return EMU_RESULT_WARN(EMU_ERR_BLOCK_FOR_TIMEOUT, src->block_idx);
        }
        utils_set_q_val(src, 0, &EN); 
        utils_set_q_val(src, 1, &current_val); 
        block_pass_results(src); 

        for (uint16_t b = 1; b <= config->chain_len; b++) {
            emu_block_struct_execution_list[src->block_idx + b]->in_set = 0;
        }

        for (uint16_t b = 1; b <= config->chain_len; b++) {
            block_handle_t* child = emu_block_struct_execution_list[src->block_idx + b];
                res = child->block_function(child);
                if (res.code != EMU_OK) {return res;}
        }

        switch(config->op) {
            case FOR_OP_ADD: current_val += step; break;
            case FOR_OP_SUB: current_val -= step; break;
            case FOR_OP_MUL: current_val *= step; break;
            case FOR_OP_DIV: 
                if (fabs(step) > DBL_EPSILON) {current_val /= step;} 
                break;
        }
    }
    emu_loop_iterator += config->chain_len;
    return res;
}

/*************************************************************************************** 
                                    PARSER

*************************************************************************************** */

static emu_err_t _emu_parse_for_doubles(uint8_t *data, uint16_t len, block_for_handle_t* handle);
static emu_err_t _emu_parse_for_config(uint8_t *data, uint16_t len, block_for_handle_t* handle);

#define CONST_PACKET 0x01
#define CONFIG_PACKET 0x02

emu_result_t emu_parse_block_for(chr_msg_buffer_t *source, uint16_t block_idx)
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

        if (data[0] == 0xbb && data[1] == BLOCK_FOR && (READ_U16(data, 3)==block_idx))
        {
            uint16_t block_idx = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_idx];
            if (block_ptr->extras == NULL) {
                block_ptr->extras = calloc(1, sizeof(block_for_handle_t));
                if (!block_ptr->extras) {
                    res.code = EMU_ERR_NO_MEM;
                    res.restart = true;
                    return res;
                }
            }
            block_for_handle_t* handle = (block_for_handle_t*)block_ptr->extras;

            if (data[2] == CONST_PACKET) { 
                if (_emu_parse_for_doubles(data, len, handle) != EMU_OK) {
                    res.code = EMU_ERR_INVALID_DATA;
                    res.restart = true;
                    return res;
                }
            }
            else if (data[2] == CONFIG_PACKET) {
                if (_emu_parse_for_config(data, len, handle) != EMU_OK) {
                    res.code = EMU_ERR_INVALID_DATA;
                    res.restart = true;
                    return res;
                }
            }  
            search_idx++;
        }
        else {
            search_idx++;
        }
    }
    return res;
}

// Helper: Parsowanie Double (Start, End, Step) - MSG TYPE 0x01
// Format: [Header:5] + [Start:8] + [End:8] + [Step:8]
static emu_err_t _emu_parse_for_doubles(uint8_t *data, uint16_t len, block_for_handle_t* handle)
{  
    size_t offset = 5; 
    memcpy(&handle->start_val, &data[offset], sizeof(double));
    offset += sizeof(double);
    
    memcpy(&handle->end_val, &data[offset], sizeof(double));
    offset += sizeof(double);
    
    memcpy(&handle->op_step, &data[offset], sizeof(double));
    LOG_I(TAG, "Start: %lf, End: %lf, Step: %lf", handle->start_val, handle->end_val, handle->op_step);
    return EMU_OK;
}

// Helper: Parsowanie Config (Chain, Cond, Op, Mask) - MSG TYPE 0x02
// Format: [Header:5] + [Chain:2] + [Cond:1] + [Op:1] + [Mask:1]
static emu_err_t _emu_parse_for_config(uint8_t *data, uint16_t len, block_for_handle_t* handle)
{
    size_t offset = 5;
    handle->chain_len = READ_U16(data, offset);
    offset += 2;
    handle->condition = (block_for_condition_t)data[offset++];
    handle->op = (block_for_operator_t)data[offset++];
    return EMU_OK;
}