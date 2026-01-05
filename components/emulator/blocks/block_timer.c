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
    emu_result_t res = EMU_RESULT_OK();
    
    bool IN = true; 
    if (block->inputs[0]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_EN], false);
        if (likely(var.error == EMU_OK)) {
            IN = (bool)emu_var_to_double(var);
        }else{
            EMU_RETURN_CRITICAL(var.error, block->cfg.block_idx, TAG, "Input acces error (EN) %s", EMU_ERR_TO_STR(var.error));
        }
    }
    
    // RST (Reset)
    bool RST = false;
    if(block->inputs[BLOCK_TIMER_IN_RST]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_RST], false);
        if (likely(var.error == EMU_OK)) {RST = (bool)emu_var_to_double(var);}
        else{EMU_RETURN_CRITICAL(var.error, block->cfg.block_idx, TAG, "Input acces error (RST) %s", EMU_ERR_TO_STR(var.error));}
    }
    
    uint32_t PT = data->default_pt; 
    if (block->inputs[BLOCK_TIMER_IN_PT]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_PT], false);
        if (var.error == EMU_OK) {PT = (uint32_t)emu_var_to_double(var);}
        else{EMU_RETURN_CRITICAL(var.error, block->cfg.block_idx, TAG, "Input acces error (PT) %s", EMU_ERR_TO_STR(var.error));}
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
    res = emu_block_set_output(block, &v_en, 0);
    if (unlikely(res.code != EMU_OK)) {
        EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output acces error %s", EMU_ERR_TO_STR(res.code));
    }
    
    // Ustawienie wyjścia ET (czasu) - brakowało tego w twoim kodzie, ale zgodnie z logiką timera powinno być
    // Zakładam, że chcesz to dodać, skoro liczysz delta_time.
    // Jeśli nie chcesz zmieniać logiki, usuń poniższy blok.
    emu_variable_t v_et = { .type = DATA_D, .data.d = (double)data->delta_time };
    res = emu_block_set_output(block, &v_et, BLOCK_TIMER_OUT_ET);
    if (unlikely(res.code != EMU_OK)) {
         EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output ET error %s", EMU_ERR_TO_STR(res.code));
    }

    return EMU_RESULT_OK();
}


#define MSG_TIMER_CONFIG 0x01

static emu_err_t _emu_parse_timer_config(uint8_t *data, uint16_t len, block_timer_t* handle) {
    size_t offset = 5;
    handle->type = (block_timer_type_t)data[offset++];

    memcpy(&handle->default_pt, &data[offset], sizeof(uint32_t));
    
    LOG_I(TAG, "Config Loaded -> Type=%d, DefaultPT=%lu ms", handle->type, (unsigned long)handle->default_pt);
    return EMU_OK;
}

emu_result_t block_timer_parse(chr_msg_buffer_t *source, block_handle_t *block) {
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
                    if (!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, i, TAG, "Custom data alloc failed");
                }
                
                if (data[2] == EMU_H_BLOCK_PACKET_CONST) {
                    if (_emu_parse_timer_config(data, len, (block_timer_t*)block->custom_data) != EMU_OK) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, i, TAG, "cfg parsing issue");
                    }
                }
            }
        }
    }
    return EMU_RESULT_OK();
}

void block_timer_free(block_handle_t *block) {
    if (block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared timer data");
    }
}
emu_result_t block_timer_verify(block_handle_t *block) {
    if (!block->custom_data) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, "BLOCK_TIMER", "Custom Data is NULL");}

    block_timer_t *data = (block_timer_t*)block->custom_data;

    if (data->type > TIMER_TYPE_TP_INV) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, "BLOCK_TIMER", "Invalid Timer Type: %d", data->type);}

    return EMU_RESULT_OK();
}