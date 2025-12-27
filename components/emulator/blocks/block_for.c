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



emu_err_t block_for(block_handle_t *src) {

    bool EN = IN_GET_B(src, 0);
    emu_err_t err = EMU_OK;
    uint8_t offset = 0;
    if(EN || src->in_used == 0){
        if (!src->extras) {
            return EMU_ERR_INVALID_DATA;
        }
        block_for_handle_t* config = (block_for_handle_t*)src->extras;
        double iterator = config->start_val;
        if (((config->what_to_use_mask & START_SETTING_MASK)>>1) == START_SETTING_MASK){
            if(((src->in_set & START_SETTING_MASK)>>1) == START_SETTING_MASK){
                iterator = IN_GET_D(src, BLOCK_FOR_IN_START);
            }else{
                utils_global_var_acces_recursive(src->global_reference[offset], &iterator);
                offset++;
            }
        }
        double limit = config->end_val;
        if (!((config->what_to_use_mask & STOP_SETTING_MASK) == STOP_SETTING_MASK)){
            if(((src->in_set & STOP_SETTING_MASK)>>1) == STOP_SETTING_MASK){
                iterator = IN_GET_D(src, BLOCK_FOR_IN_STOP);
            }else{
                utils_global_var_acces_recursive(src->global_reference[offset], &limit);
                offset++;
            }
        }
        double step = config->op_step;
        if (!((config->what_to_use_mask & STEP_SETTING_MASK) == STEP_SETTING_MASK)){
            if(((src->in_set & STEP_SETTING_MASK)>>1) == STEP_SETTING_MASK){
                iterator = IN_GET_D(src, BLOCK_FOR_IN_STEP);
            }else{
                utils_global_var_acces_recursive(src->global_reference[offset], &iterator);
                offset++;
            }
        }
        LOG_I(TAG, "iterator = %lf, limit = %lf, step = %lf", iterator, limit, step);
        uint16_t watchdog = 0;

        while(1) {
            bool condition_met = false;
            LOG_I(TAG, "condition %d", config->condition);
            switch (config->condition) {
                case FOR_COND_GT:
                    if (iterator > (limit + DBL_EPSILON)) condition_met = true;
                    break;
                case FOR_COND_LT:
                    if (iterator < (limit - DBL_EPSILON)) condition_met = true;
                    break;
                case FOR_COND_GTE:
                    if (iterator >= (limit - DBL_EPSILON)) condition_met = true;
                    break;
                case FOR_COND_LTE:
                    if (iterator <= (limit + DBL_EPSILON)) condition_met = true;
                    break;
                default:
                    condition_met = false;
                    break;
            }
            
            watchdog ++;
            LOG_I(TAG, "watchdog = %d", watchdog);
            if (!condition_met) break;
            if (watchdog > BLOCK_FOR_MAX_CYCLES) {
                LOG_W(TAG, "Max for loop iterations exceeding block: %d", src->block_idx);
                return EMU_ERR_BLOCK_FOR_TIMEOUT;
                break;
            }

            for (uint16_t b = 1; b <= config->chain_len; b++) {
                block_handle_t* child = emu_block_struct_execution_list[src->block_idx + b];
                if (child) {
                    child->in_set = 0;
                }
            }
            utils_set_q_val(src, 0, &EN);
            utils_set_q_val(src, 1, &iterator);
            block_pass_results(src);
            for (uint16_t b = 1; b <= config->chain_len; b++) {
                block_handle_t* child = emu_block_struct_execution_list[src->block_idx + b];
                if (child && child->block_function) {
                    err = child->block_function(child);
                    if (err != EMU_OK) {
                        LOG_I(TAG, "During execution of block %d error %s", src->block_idx + b, EMU_ERR_TO_STR(err));
                        return err;
                    }
                }
            }
            switch(config->op) {
                case FOR_OP_ADD:
                    iterator += step;
                    break;
                case FOR_OP_SUB:
                    iterator -= step;
                    break;
                case FOR_OP_MUL:
                    iterator *= step;
                    break;
                case FOR_OP_DIV:
                    if (fabs(step) > DBL_EPSILON) {
                        iterator /= step;
                    }
                    break;
                default:
                    break;
            }
        }

        emu_loop_iterator += config->chain_len;

        return EMU_OK;
    }
    return EMU_OK;
}

/*************************************************************************************** 
                                    PARSER

*************************************************************************************** */

static emu_err_t _emu_parse_for_doubles(uint8_t *data, uint16_t len, block_for_handle_t* handle);
static emu_err_t _emu_parse_for_config(uint8_t *data, uint16_t len, block_for_handle_t* handle);

#define CONST_PACKET 0x01
#define CONFIG_PACKET 0x02

emu_err_t emu_parse_for_blocks(chr_msg_buffer_t *source)
{
    uint8_t *data;
    size_t len;
    block_handle_t *block_ptr;
    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx = 0;

    while(search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[1] == BLOCK_FOR)
        {
            uint16_t block_idx = READ_U16(data, 3);
            block_ptr = emu_block_struct_execution_list[block_idx];

            if (block_ptr->extras == NULL) {
                block_ptr->extras = calloc(1, sizeof(block_for_handle_t));
                if (!block_ptr->extras) {
                    return EMU_ERR_NO_MEM;
                }
            }

            block_for_handle_t* handle = (block_for_handle_t*)block_ptr->extras;

            if (data[2] == CONST_PACKET) { 
                if (_emu_parse_for_doubles(data, len, handle) != EMU_OK) {
                    return EMU_ERR_INVALID_DATA;
                }
            }
            else if (data[2] == CONFIG_PACKET) {
                if (_emu_parse_for_config(data, len, handle) != EMU_OK) {
                    return EMU_ERR_INVALID_DATA;
                }
            }  
            search_idx++;
        }
        else {
            search_idx++;
        }
    }
    return EMU_OK;
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
    handle->what_to_use_mask = data[offset++];

    return EMU_OK;
}