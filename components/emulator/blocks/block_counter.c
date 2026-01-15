#include "block_counter.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include <string.h>

static const char* TAG = __FILE_NAME__;


typedef enum{
    CFG_ON_RISING,
    CFG_WHEN_ACTIVE,
}counter_cfg_t;

typedef struct{
    double current_val;
    double step;
    double max;
    double min;
    double start;
    counter_cfg_t cfg;
    bool prev_ctu; 
    bool prev_ctd;
}counter_handle_t;

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



emu_result_t block_counter(block_handle_t *block) {

    counter_handle_t* data = (counter_handle_t*)block->custom_data;
    emu_result_t res;
    (void)res; // May be used by macros

    if (block_in_updated(block, IN_3_STEP)) {MEM_GET(&data->step, block->inputs[IN_3_STEP]);}


    if (block_in_updated(block, IN_4_LIMIT_MAX)) {MEM_GET(&data->max, block->inputs[IN_4_LIMIT_MAX]);}
    
    if (block_in_updated(block, IN_5_LIMIT_MIN)) {MEM_GET(&data->min, block->inputs[IN_5_LIMIT_MIN]);}


    // RESET (Priority 1)
    if (block_check_EN(block, IN_2_RESET)) {
            data->current_val = data->start;
            data->prev_ctu = false; // Clear edge detection state
            data->prev_ctd = false;
            goto finish; // Exit immediately after reset
    }

    // CTU
    
    if (block_check_EN(block, IN_0_CTU)) {
        
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

        if (block_check_EN(block, IN_1_CTD)) {
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
LOG_I(TAG, "FU");
return EMU_RESULT_OK();
finish: 
    //LOG_I(TAG, "CURRENT, START, STEP, MAX, MIN: %lf, %lf, %lf, %lf, %lf", data->current_val, start, data->step, data->max, data->min);
    // OUT_0: ENO (True/Active)
    LOG_I(TAG, "Setting outputs in counter block");
    emu_variable_t v_eno = { .type = DATA_B, .data.b = true };
    res = block_set_output(block, &v_eno, OUT_0_ENO);
    if (unlikely(res.code != EMU_OK)) EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_counter, block->cfg.block_idx, 0, TAG, "Set ENO Error");

    // OUT_1: VAL (Current Value)
    emu_variable_t v_val = { .type = DATA_D, .data.d = data->current_val };
    res = block_set_output(block, &v_val, OUT_1_VAL);
    if (unlikely(res.code != EMU_OK)) EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_counter, block->cfg.block_idx, 0, TAG, "Set VAL Error");

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
            if(!handle) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_counter_parse, block->cfg.block_idx, 0, TAG, "[%d]Null handle ptr", block->cfg.block_idx);}
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
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_block_counter_parse, block->cfg.block_idx, 0, TAG, "[%d]Packet not found", block->cfg.block_idx);
}

emu_result_t block_counter_verify(block_handle_t *block) {
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_counter_verify, block->cfg.block_idx, 0, TAG, "Custom Data is NULL %d", block->cfg.block_idx);}
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


