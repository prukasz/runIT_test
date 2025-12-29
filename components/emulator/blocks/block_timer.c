#include "block_timer.h"
#include "emulator_loop.h"
#include "emulator_errors.h"
#include "utils_block_in_q_access.h"
#include "utils_parse.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "BLOCK_TIMER";

// Globalny uchwyt pętli
extern emu_loop_handle_t loop_handle; 

// Definicje indeksów
#define BLOCK_TIMER_IN_EN   0
#define BLOCK_TIMER_IN_PT   1
#define BLOCK_TIMER_IN_RST  2

#define BLOCK_TIMER_OUT_Q   0
#define BLOCK_TIMER_OUT_ET  1

/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

emu_result_t block_timer(block_handle_t *block) {
    if (!block || !block->extras) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    
    // 1. Pobierz czas absolutny
    if (!loop_handle) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    uint64_t now_ms = loop_handle->timer.time;

    // 2. Pobierz Wejścia
    double temp_val = 0;

    // IN (Enable)
    bool IN = true; 
    if (utils_get_in_val_auto(block, BLOCK_TIMER_IN_EN, &temp_val) == EMU_OK) {
        IN = (bool)temp_val;
    }

    // RST (Reset)
    bool RST = false; 
    if (utils_get_in_val_auto(block, BLOCK_TIMER_IN_RST, &temp_val) == EMU_OK) {
        RST = (bool)temp_val;
    }

    block_timer_t* data = (block_timer_t*)block->extras;

    // PT (Preset Time)
    uint32_t PT = data->default_pt; 
    if (utils_get_in_val_auto(block, BLOCK_TIMER_IN_PT, &temp_val) == EMU_OK) {
        PT = (uint32_t)temp_val; 
    }
    block_timer_type_t type = data->type;

    if (RST) {
        data->delta_time = 0;
        if (type == TIMER_TYPE_TOF){
            data->q_out = true;
        }else{
            data->q_out = false;
        }
        data->counting = false;
        data->start_time = 0; 
    } 
    else {
        switch (type) {
            case TIMER_TYPE_TON:
                if(IN){
                    if(data->counting){
                        data->delta_time = now_ms - data->start_time;
                        if(data->delta_time >= PT){
                            data->q_out = true;
                        }
                        data->delta_time = now_ms - data->start_time;
                    }
                    else{
                        data->start_time = now_ms;
                        data->counting = true;
                        data->q_out = false;
                    }
                }else{ 
                    //reset when input inactive
                    data->delta_time = 0;
                    data->q_out = false;
                    data->counting = false;
                    data->start_time = 0; 
                }
            break;
            case TIMER_TYPE_TOF:
                if(IN){
                    if(data->counting){
                        data->delta_time = now_ms - data->start_time;
                        if(data->delta_time >= PT){
                            LOG_I(TAG, "TOF triggered Q = 0");
                            data->q_out = false;
                        }
                    }
                    else{
                        data->start_time = now_ms;
                        data->counting = true;
                        data->q_out = true;
                    }
                }else{ 
                    data->delta_time = 0;
                    data->q_out = true;
                    data->counting = false;
                    data->start_time = 0; 
                }
                break;
            case TIMER_TYPE_TP:
                if(IN){
                    //check if not counting and prev_in low to enforce Rising edge
                    if(!data->counting && !data->prev_in){
                        data->counting = true;
                        data->start_time = now_ms;
                        data->q_out = true;
                    }else if(data->counting){
                        data->delta_time = now_ms - data->start_time;
                        if(data->delta_time >= PT){
                            data->q_out = false;
                            data->counting = false;
                        }
                    }
                }else{
                    //continue even if input is low to finish pulse
                    if(data->counting){
                        data->delta_time = now_ms - data->start_time;
                        if(data->delta_time >= PT){
                            data->q_out = false;
                            data->counting = false;
                        }
                    }
                }
                break;
            default:
                break;
        }
    }
    data->prev_in = IN;
    LOG_I(TAG, "EN %d, RST %d, Q val %d, et %lld ms",IN, RST,  data->q_out, data->start_time);
    utils_set_q_val(block, BLOCK_TIMER_OUT_Q,  &data->q_out);
    utils_set_q_val(block, BLOCK_TIMER_OUT_ET, &data->start_time);
    block_pass_results(block);
    return EMU_RESULT_OK();
}

/* ========================================================================= */
/* PARSER                                                                    */
/* ========================================================================= */

#define MSG_TIMER_CONFIG 0x01

static emu_err_t _emu_parse_timer_config(uint8_t *data, uint16_t len, block_timer_t* handle) {
    if (len < 10) {
        ESP_LOGE(TAG, "Parser: Data too short (%d)", len);
        return EMU_ERR_INVALID_DATA;
    }

    size_t offset = 5;
    handle->type = (block_timer_type_t)data[offset++];
    memcpy(&handle->default_pt, &data[offset], sizeof(uint32_t));
    
    ESP_LOGI(TAG, "PARSER: Config Loaded -> Type=%d (1=TON, 2=TOF, 3=TP), DefaultPT=%lu ms", handle->type, handle->default_pt);
    return EMU_OK;
}

emu_result_t emu_parse_block_timer(chr_msg_buffer_t *source, block_handle_t *block) {
    emu_result_t res = EMU_RESULT_OK();
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (data[0] == 0xBB && data[1] == BLOCK_TIMER && READ_U16(data, 3) == block->block_idx) {
            
            if (block->extras == NULL) {
                ESP_LOGI(TAG, "[%d] Allocating memory for extras", block->block_idx);
                block->extras = calloc(1, sizeof(block_timer_t));
                if (!block->extras) {
                    ESP_LOGE(TAG, "[%d] Malloc failed!", block->block_idx);
                    return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, block->block_idx);
                }
            }
            
            if (data[2] == MSG_TIMER_CONFIG) {
                if (_emu_parse_timer_config(data, len, (block_timer_t*)block->extras) != EMU_OK) {
                    ESP_LOGE(TAG, "[%d] Config parse error", block->block_idx);
                    return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_DATA, block->block_idx);
                }
            }
        }
    }
    return res;
}