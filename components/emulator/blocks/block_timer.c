#include "block_timer.h"
#include "emulator_loop.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h" 
#include "emulator_blocks.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "BLOCK_TIMER";

extern emu_loop_handle_t loop_handle; 

#define BLOCK_TIMER_IN_EN   0
#define BLOCK_TIMER_IN_PT   1
#define BLOCK_TIMER_IN_RST  2

#define BLOCK_TIMER_OUT_Q   0
#define BLOCK_TIMER_OUT_ET  1

/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

emu_result_t block_timer(block_handle_t *block) {


    uint64_t now_ms = loop_handle->timer.time;
    block_timer_t* data = (block_timer_t*)block->custom_data;
    emu_variable_t var;
    if(!data){
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx);
    }
    emu_result_t res = EMU_RESULT_OK();

    bool IN = true; 
    if (block->inputs[0]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_EN], BLOCK_TIMER_IN_EN);
        if (likely(var.error == EMU_OK)) {
            IN = (bool)emu_var_to_double(var);
        }else{
            res.code = EMU_ERR_BLOCK_INACTIVE;
            res.notice = true;
            res.block_idx = block->cfg.block_idx;
            return res;
        }
    }
    
    // RST (Reset)
    bool RST = false;
    if(block->inputs[BLOCK_TIMER_IN_RST]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_RST], BLOCK_TIMER_IN_RST);
        if (likely(var.error == EMU_OK)) {
            RST = (bool)emu_var_to_double(var);
        }
    }
    

    uint32_t PT = data->default_pt; 
    if (block->inputs[BLOCK_TIMER_IN_PT]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_PT], BLOCK_TIMER_IN_PT);
        if (var.error == EMU_OK) {
            PT = (uint32_t)emu_var_to_double(var); 
        }
    }

    block_timer_type_t type = data->type;

    LOG_I(TAG, "Type: %d, PT: %ld,  delta: %ld, now %lld", type, PT, data->delta_time, now_ms);
    if (RST) {
        data->delta_time = 0;
        data->q_out = (type == TIMER_TYPE_TOF); // TOF w resecie ma Q=1 (zazwyczaj)
        data->counting = false;
        data->start_time = 0; 
    } 
    else {
        switch (type) {
            case TIMER_TYPE_TON:
                if (IN) {
                    if (!data->counting) {
                        data->start_time = now_ms;
                        data->counting = true;
                        data->q_out = false;
                    } else {
                        data->delta_time = now_ms - data->start_time;
                        if (data->delta_time >= PT) {
                            data->q_out = true;
                            data->delta_time = PT; // Limituj ET do PT
                        }
                    }
                } else { 
                    data->delta_time = 0;
                    data->q_out = false;
                    data->counting = false;
                }
                break;

            case TIMER_TYPE_TOF:
                if (IN) {
                    if (!data->counting && !data->prev_in) {
                        data->start_time = now_ms;
                        data->counting = true;
                    }
                    
                    if (data->counting) {
                        data->delta_time = now_ms - data->start_time;
                        if (data->delta_time >= PT) {
                            data->q_out = false;
                            data->counting = false;
                            data->delta_time = PT;
                        } else {
                            data->q_out = true;
                        }
                    } else {
                        data->q_out = false;
                        data->delta_time = 0;
                    }
                } else { 
                    data->delta_time = 0;
                    data->q_out = true;
                    data->counting = false;
                }
                break;

            case TIMER_TYPE_TP:
                if (IN && !data->prev_in && !data->counting) {
                    data->counting = true;
                    data->start_time = now_ms;
                    data->q_out = true;
                }

                if (data->counting) {
                    data->delta_time = now_ms - data->start_time;
                    if (data->delta_time >= PT) {
                        data->q_out = false;
                        data->counting = false;
                        data->delta_time = PT;
                    } else {
                        data->q_out = true;
                    }
                }
                break;

            default:
                break;
        }
    }

    data->prev_in = IN;
    
    emu_variable_t v_en = { .type = DATA_B, .data.b = data->q_out};
    LOG_I(TAG, "is out active %d", data->q_out);
    emu_block_set_output(block, &v_en, 0);
    return EMU_RESULT_OK();
}


#define MSG_TIMER_CONFIG 0x01

static emu_err_t _emu_parse_timer_config(uint8_t *data, uint16_t len, block_timer_t* handle) {
    size_t offset = 5;
    handle->type = (block_timer_type_t)data[offset++];
    
    // Używamy bezpiecznego odczytu PT
    memcpy(&handle->default_pt, &data[offset], sizeof(uint32_t));
    
    LOG_I(TAG, "Config Loaded -> Type=%d, DefaultPT=%lu ms", 
             handle->type, (unsigned long)handle->default_pt);
    return EMU_OK;
}

emu_result_t emu_parse_block_timer(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (data[0] == 0xBB && data[1] == BLOCK_TIMER) {
            uint16_t block_idx = 0;
            memcpy(&block_idx, &data[3], 2);
            if(block_idx==block->cfg.block_idx){
            
                if (block->custom_data == NULL) {
                    block->custom_data = calloc(1, sizeof(block_timer_t));
                    if (!block->custom_data) return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, i);
                }
                
                if (data[2] == MSG_TIMER_CONFIG) {
                    if (_emu_parse_timer_config(data, len, (block_timer_t*)block->custom_data) != EMU_OK) {
                        return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_DATA, i);
                    }
                }
            }
        }
    }
    return EMU_RESULT_OK();
}