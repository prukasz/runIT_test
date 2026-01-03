#include "block_timer.h"
#include "emulator_loop.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h" 
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
    if (!block || !block->custom_data) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }
    
    if (!loop_handle) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    uint64_t now_ms = loop_handle->timer.time;

    block_timer_t* data = (block_timer_t*)block->custom_data;
    emu_variable_t var;

    // --- POBIERANIE WEJŚĆ ---

    // IN (Enable)
    bool IN = false; 
    var = utils_get_in_val_auto(block, BLOCK_TIMER_IN_EN);
    if (var.error == EMU_OK) {
        IN = (bool)emu_var_to_double(var);
    }

    // RST (Reset)
    bool RST = false; 
    var = utils_get_in_val_auto(block, BLOCK_TIMER_IN_RST);
    if (var.error == EMU_OK) {
        RST = (bool)emu_var_to_double(var);
    }

    // PT (Preset Time) - jeśli brak wejścia, użyj domyślnego z konfiguracji
    uint32_t PT = data->default_pt; 
    var = utils_get_in_val_auto(block, BLOCK_TIMER_IN_PT);
    if (var.error == EMU_OK) {
        PT = (uint32_t)emu_var_to_double(var); 
    }

    block_timer_type_t type = data->type;

    // --- LOGIKA TIMERA ---

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
                if (!IN) {
                    if (!data->counting && data->prev_in) { // Zbocze opadające IN
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
                    // Gdy IN jest wysokie, Q jest wysokie, timer stoi
                    data->delta_time = 0;
                    data->q_out = true;
                    data->counting = false;
                }
                break;

            case TIMER_TYPE_TP:
                // Start na zboczu narastającym, kontynuuj do końca PT niezależnie od IN
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

    // --- WYJŚCIA ---
    // Q (Boolean)
    utils_set_q_val(block, BLOCK_TIMER_OUT_Q, &data->q_out);
    
    // ET (Elapsed Time - zazwyczaj jako UI32 lub Float w ms)
    // Wysyłamy delta_time (czas który upłynął), nie start_time
    utils_set_q_val(block, BLOCK_TIMER_OUT_ET, &data->delta_time);

    block_pass_results(block);
    return EMU_RESULT_OK();
}

/* ========================================================================= */
/* PARSER (Pozostaje w dużej mierze taki sam, poprawiono tylko logi)         */
/* ========================================================================= */

#define MSG_TIMER_CONFIG 0x01

static emu_err_t _emu_parse_timer_config(uint8_t *data, uint16_t len, block_timer_t* handle) {
    if (len < 10) {
        return EMU_ERR_INVALID_DATA;
    }

    size_t offset = 5;
    handle->type = (block_timer_type_t)data[offset++];
    
    // Używamy bezpiecznego odczytu PT
    memcpy(&handle->default_pt, &data[offset], sizeof(uint32_t));
    
    ESP_LOGI(TAG, "PARSER: Config Loaded -> Type=%d, DefaultPT=%lu ms", 
             handle->type, (unsigned long)handle->default_pt);
    return EMU_OK;
}

emu_result_t emu_parse_block_timer(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Nagłówek pakietu bloku
        if (data[0] == 0xBB && data[1] == BLOCK_TIMER && READ_U16(data, 3) == block->cfg.block_idx) {
            
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
    return EMU_RESULT_OK();
}