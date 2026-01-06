#include "block_counter.h"
#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include <string.h>

static const char* TAG = "BLOCK_COUNTER";

// Input indices
#define IN_0_CTU        0
#define IN_1_CTD        1
#define IN_2_RESET      2
#define IN_3_STEP       3   
#define IN_4_LIMIT_MAX  4
#define IN_5_LIMIT_MIN  5

// Output indices
#define OUT_0_ENO       0
#define OUT_1_VAL       1

/* ========================================================================= */
/* EXECUTION LOGIC                                                           */
/* ========================================================================= */

emu_result_t block_counter(block_handle_t *block) {

    counter_handle_t* data = (counter_handle_t*)block->custom_data;
    emu_result_t res;
    emu_variable_t var;

    if (block->inputs[IN_3_STEP] && emu_block_check_input_updated(block, IN_3_STEP)) {
        var = mem_get(block->inputs[IN_3_STEP], false);
        if (likely(var.error == EMU_OK)) data->step = emu_var_to_double(var);
    }
    // MAX
    if (block->inputs[IN_4_LIMIT_MAX] && emu_block_check_input_updated(block, IN_4_LIMIT_MAX)) {
        var = mem_get(block->inputs[IN_4_LIMIT_MAX], false);
        if (likely(var.error == EMU_OK)) data->max = emu_var_to_double(var);
    }
    // MIN
    if (block->inputs[IN_5_LIMIT_MIN] && emu_block_check_input_updated(block, IN_5_LIMIT_MIN)) {
        var = mem_get(block->inputs[IN_5_LIMIT_MIN], false);
        if (likely(var.error == EMU_OK)) data->min = emu_var_to_double(var);
    }

    // RESET (Priority 1)
    if (emu_block_check_en(block, IN_2_RESET)) {
            data->current_val = data->start;
            data->prev_ctu = false; // Clear edge detection state
            data->prev_ctd = false;
            goto finish; // Exit immediately after reset
    }

    // CTU
    

    if (emu_block_check_en(block, IN_0_CTU)) {
        
        if (data->cfg == CFG_ON_RISING) {
            if (!data->prev_ctu){
                data->current_val += data->step;
                data->prev_ctu = true;
                if (data->current_val > data->max) data->current_val = data->max;
                goto finish; // CTU handled, skip CTD
            }    
        }else{
            data->current_val += data->step;
            if (data->current_val > data->max) data->current_val = data->max;
            data->prev_ctu = true;
            goto finish;
        }
    }else{
        data->prev_ctu = false;
    }

        if (emu_block_check_en(block, IN_0_CTU)) {
        if (data->cfg == CFG_ON_RISING) {
            if (!data->prev_ctd){
                data->current_val -= data->step;
                data->prev_ctd = true;
                if (data->current_val < data->min) data->current_val = data->min;
                goto finish; // CTU handled, skip CTD
            }    
        }else{
            data->current_val -= data->step;
            if (data->current_val < data->min) data->current_val = data->min;
            data->prev_ctd = true;
            goto finish;
        }
    }else{
        data->prev_ctu = false;
    }

return EMU_RESULT_OK();
finish: 
    LOG_I(TAG, "CURRENT, START, STEP, MAX, MIN: %lf, %lf, %lf, %lf, %lf", data->current_val, start, data->step, data->max, data->min);
    // OUT_0: ENO (True/Active)
    emu_variable_t v_eno = { .type = DATA_B, .data.b = true };
    res = emu_block_set_output(block, &v_eno, OUT_0_ENO);
    if (unlikely(res.code != EMU_OK)) EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Set ENO Error");

    // OUT_1: VAL (Current Value)
    emu_variable_t v_val = { .type = DATA_D, .data.d = data->current_val };
    res = emu_block_set_output(block, &v_val, OUT_1_VAL);
    if (unlikely(res.code != EMU_OK)) EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Set VAL Error");

    return EMU_RESULT_OK();
}

emu_result_t block_counter_parse(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len > 5 && data[0] == 0xBB && data[1] == BLOCK_COUNTER && data[2] == EMU_H_BLOCK_PACKET_CUSTOM) {
            uint16_t block_idx = 0;
            memcpy(&block_idx, &data[3], 2);
            if (block_idx == block->cfg.block_idx) {

            block->custom_data = calloc(1, sizeof(counter_handle_t));
            counter_handle_t* handle = (counter_handle_t*)block->custom_data;
            if(!handle) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block->cfg.block_idx,TAG, "[%d]Null handle ptr", block->cfg.block_idx);}
            size_t offset = 5; // Skip header bytes
            handle->cfg = (counter_cfg_t)data[offset++];
            
            memcpy(&handle->start, &data[offset], 8); offset += 8;
            memcpy(&handle->step,  &data[offset], 8); offset += 8;
            memcpy(&handle->max,   &data[offset], 8); offset += 8;
            memcpy(&handle->min,   &data[offset], 8); offset += 8;
            
            handle->current_val = handle->start;
            handle->prev_ctu = false;
            handle->prev_ctd = false;

            return EMU_RESULT_OK();
            }
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, block->cfg.block_idx, TAG, "[%d]Packet not found", block->cfg.block_idx);
}

emu_result_t block_counter_verify(block_handle_t *block) {
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, TAG, "Custom Data is NULL %d", block->cfg.block_idx);}
    return EMU_RESULT_OK();
}

void block_counter_free(block_handle_t* block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "[%d]Cleared counter block data", block->cfg.block_idx);
    }
    return;
}


