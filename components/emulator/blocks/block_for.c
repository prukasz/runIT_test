#include "block_for.h"
#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_blocks_functions_list.h" // Tutaj musi byc deklaracja blocks_functions_table
#include "emulator_loop.h"
#include "esp_log.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "BLOCK_FOR";

// Zmienne globalne emulatora
extern block_handle_t **emu_block_struct_execution_list; 
extern uint16_t emu_loop_iterator;
extern volatile emu_loop_handle_t loop_handle;
// Definicje indeksów wejść
#define BLOCK_FOR_IN_START 1
#define BLOCK_FOR_IN_STOP  2
#define BLOCK_FOR_IN_STEP  3

/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

emu_result_t block_for(block_handle_t *block) {
    emu_result_t res = {.code = EMU_OK};
    emu_variable_t var;
    
    // 1. Synchronizacja: Sprawdź czy wejścia są gotowe
    if (!emu_block_check_inputs_updated(block)) {
        res.code = EMU_ERR_BLOCK_INACTIVE;
        res.notice = true;
        res.block_idx = block->cfg.block_idx;
        return res;
    }

    bool EN = true; 
    if (block->inputs[0]){
        var = mem_get(block->inputs[BLOCK_TIMER_IN_EN], BLOCK_TIMER_IN_EN);
        if (likely(var.error == EMU_OK)) {
            EN = (bool)emu_var_to_double(var);
        }else{
            res.code = EMU_ERR_BLOCK_INACTIVE;
            res.notice = true;
            res.block_idx = block->cfg.block_idx;
            return res;
        }

    }
    if(!EN){
        res.code = EMU_ERR_BLOCK_INACTIVE;
        res.notice = true;
        res.block_idx = block->cfg.block_idx;
        return res;
    }


    block_for_handle_t* config = (block_for_handle_t*)block->custom_data;
    if (unlikely(!config)) {return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx);}

    // Start Value
    double iterator_start = config->start_val;
    if (block->inputs[BLOCK_FOR_IN_START]) {
        var = mem_get(block->inputs[BLOCK_FOR_IN_START], false);
        if (likely(var.error == EMU_OK)) {iterator_start = emu_var_to_double(var);}
    }

    double limit = config->end_val;
    if (block->inputs[BLOCK_FOR_IN_STOP]) {
        var = mem_get(block->inputs[BLOCK_FOR_IN_STOP], false);
        if (likely(var.error == EMU_OK)) {limit = emu_var_to_double(var);}
    }

    // Step Value
    double step = config->op_step;
    if (block->inputs[BLOCK_FOR_IN_STEP]) {
        var = mem_get(block->inputs[BLOCK_FOR_IN_STEP], false);
        if (likely(var.error == EMU_OK)) {step = emu_var_to_double(var);}
    }

    double current_val = iterator_start;
    uint16_t watchdog = 0;

    LOG_I(TAG, "FOR [%d] Start: %.2f End: %.2f Step: %.2f", block->cfg.block_idx, iterator_start, limit, step);

    while(1) {
        bool condition_met = false;
        if(loop_handle->wtd.wtd_triggered){
            return EMU_RESULT_CRITICAL(EMU_ERR_BLOCK_FOR_TIMEOUT, block->cfg.block_idx);
        }
        
        // Sprawdzenie warunku końca
        switch (config->condition) {
            case FOR_COND_GT:  condition_met = (current_val > (limit + DBL_EPSILON)); break;
            case FOR_COND_LT:  condition_met = (current_val < (limit - DBL_EPSILON)); break;
            case FOR_COND_GTE: condition_met = (current_val >= (limit - DBL_EPSILON)); break;
            case FOR_COND_LTE: condition_met = (current_val <= (limit + DBL_EPSILON)); break;
            default: break;
        }

        if (!condition_met) break;

        // Ustawienie wyjść bloku FOR (dostępne dla dzieci w tej iteracji)
        // Q0 = EN (True)
        emu_variable_t v_en = { .type = DATA_B, .data.b = true };
        emu_block_set_output(block, &v_en, 0);

        // Q1 = Iterator (Double)
        emu_variable_t v_iter = { .type = DATA_D, .data.d = current_val };
        emu_block_set_output(block, &v_iter, 1);
        LOG_I(TAG, "executing loop[%d] time", watchdog);

        for (uint16_t b = 1; b <= config->chain_len; b++) {
            // Pobieramy dziecko z listy globalnej (następne indeksy po FOR)
            block_handle_t* child = emu_block_struct_execution_list[block->cfg.block_idx + b];
            
            emu_block_reset_outputs_status(child);
            uint8_t child_type = child->cfg.block_type;
            emu_block_func child_func = blocks_functions_table[child_type];
            if (likely(child_func)) {
                res = child_func(child);
                if (unlikely(res.code != EMU_OK && res.code != EMU_ERR_BLOCK_INACTIVE)) {return res;}
            }
        }

        // Aktualizacja licznika pętli
        switch(config->op) {
            case FOR_OP_ADD: current_val += step; break;
            case FOR_OP_SUB: current_val -= step; break;
            case FOR_OP_MUL: current_val *= step; break;
            case FOR_OP_DIV: 
                if (fabs(step) > DBL_EPSILON) current_val /= step; 
                break;
        }
    }

    emu_loop_iterator += config->chain_len;
    return res;
}

/* ========================================================================= */
/* PARSER                                                                    */
/* ========================================================================= */

#define CONST_PACKET 0x01
#define CONFIG_PACKET 0x02

static emu_err_t _emu_parse_for_doubles(uint8_t *data, uint16_t len, block_for_handle_t* handle) { 
    LOG_I(TAG, "Parsing for doubles");
    if (len < 29) return EMU_ERR_INVALID_DATA; // 5 + 3*8
    size_t offset = 5; 
    
    // Używamy memcpy dla bezpieczeństwa przy braku wyrównania pamięci
    memcpy(&handle->start_val, &data[offset], 8); offset += 8;
    memcpy(&handle->end_val,   &data[offset], 8); offset += 8;
    memcpy(&handle->op_step,   &data[offset], 8);
    
    return EMU_OK;
}

static emu_err_t _emu_parse_for_config(uint8_t *data, uint16_t len, block_for_handle_t* handle) {
    LOG_I(TAG, "Parsing for config");
    if (len < 9) return EMU_ERR_INVALID_DATA;
    size_t offset = 5;
    
    memcpy(&handle->chain_len, &data[offset], 2); 
    offset += 2;
    
    handle->condition = (block_for_condition_t)data[offset++];
    handle->op = (block_for_operator_t)data[offset++];
    
    return EMU_OK;
}

emu_result_t emu_parse_block_for(chr_msg_buffer_t *source, block_handle_t *block) {
    LOG_I(TAG, "Parsing block for");
    if (!block) return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0);

    emu_result_t res = {.code = EMU_OK, .block_idx = block->cfg.block_idx};
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    uint16_t target_idx = block->cfg.block_idx;

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Header check: [BB] [Type] [SubType] [ID:2]
        if (len > 3 && data[0] == 0xBB && data[1] == BLOCK_FOR) {
            
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);

            if (packet_blk_id == target_idx) {
                
                // Alokacja custom_data jeśli nie istnieje
                if (block->custom_data == NULL) {
                    block->custom_data = calloc(1, sizeof(block_for_handle_t));
                    if (!block->custom_data) {
                        return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, target_idx);
                    }
                }
                
                block_for_handle_t* handle = (block_for_handle_t*)block->custom_data;

                if (data[2] == CONST_PACKET) { 
                    _emu_parse_for_doubles(data, len, handle);
                }
                else if (data[2] == CONFIG_PACKET) {
                    _emu_parse_for_config(data, len, handle);
                }  
            }
        }
    }
    return res;
}

void emu_parse_block_for_free(block_handle_t *block) {

}