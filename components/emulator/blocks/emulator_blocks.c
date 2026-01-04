#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_variables.h"
#include "emulator_blocks_functions_list.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h> // For round()

extern emu_mem_t* s_mem_contexts[];
static const char* TAG = "EMU_BLOCKS";


// Definicje nagłówków protokołu
#define EMU_H_BLOCK_CNT     0xB000
#define EMU_H_BLOCK_START   0xBB
#define BLOCK_SUBTYPE_CFG   0x00

/* ============================================================================
    ZARZĄDZANIE PAMIĘCIĄ BLOKÓW (FREE)
   ============================================================================ */

static void free_single_block(block_handle_t* block) {
    if (!block) return;
    
    if (block->inputs) {
        free(block->inputs);
        block->inputs = NULL;
    }

    if (block->outputs) {
        free(block->outputs);
        block->outputs = NULL;
    }

    // Custom data should be freed by specific block logic or here if generic
    if (block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
    }

    free(block);
}

void emu_blocks_free_all(block_handle_t** blocks_list, uint16_t blocks_cnt) {
    if (!blocks_list) {
        LOG_W(TAG, "No block list to free");
        return;
    }
    
    for (size_t i = 0; i < blocks_cnt; i++) {
        if (blocks_list[i]) {
            free_single_block(blocks_list[i]);
            blocks_list[i] = NULL;
        }
    }
    
    free(blocks_list);
    LOG_I(TAG, "All %d blocks freed", blocks_cnt);
}

/* ============================================================================
    PARSOWANIE: LICZBA BLOKÓW I ALOKACJA LISTY
   ============================================================================ */

emu_result_t emu_parse_total_block_cnt(chr_msg_buffer_t *source, block_handle_t ***blocks_list, uint16_t *blocks_total_cnt) {
    if (!source || !blocks_list || !blocks_total_cnt) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF);
    }

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Oczekujemy [B0 00] [Count:2B]
        if (len < 4) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == EMU_H_BLOCK_CNT) {
            uint16_t cnt = 0;
            memcpy(&cnt, &data[2], 2);

            if (cnt == 0) {
                LOG_W(TAG, "Block count is 0");
                return EMU_RESULT_OK();
            }

            // Alokacja głównej tablicy wskaźników
            *blocks_list = (block_handle_t**)calloc(cnt, sizeof(block_handle_t*));
            
            if (*blocks_list == NULL) {
                LOG_E(TAG, "Failed to allocate list for %d blocks", cnt);
                return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, 0);
            }

            *blocks_total_cnt = cnt;
            LOG_I(TAG, "Allocated list for %d blocks", cnt);
            
            return EMU_RESULT_OK();
        }
    }

    return EMU_RESULT_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_H_BLOCK_CNT);
}

/* ============================================================================
    PARSOWANIE: KONFIGURACJA POJEDYNCZEGO BLOKU
   ============================================================================ */

emu_result_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t **blocks_list, uint16_t blocks_total_cnt) {
    if (!blocks_list || blocks_total_cnt == 0) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, 0);
    }

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint16_t found_cnt = 0;

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Header Check: [BB] [Type] [Subtype=00] [ID:2] ... Payload
        if (len < 9) continue;

        if (data[0] == EMU_H_BLOCK_START && data[2] == BLOCK_SUBTYPE_CFG) {
            
            uint16_t blk_idx = 0;
            memcpy(&blk_idx, &data[3], 2);

            // Safety Check
            if (blk_idx >= blocks_total_cnt) {
                LOG_E(TAG, "Block ID %d exceeds total %d", blk_idx, blocks_total_cnt);
                return EMU_RESULT_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, i);
            }

            // Alokacja struktury bloku (jeśli jeszcze nie istnieje)
            if (blocks_list[blk_idx] == NULL) {
                blocks_list[blk_idx] = (block_handle_t*)calloc(1, sizeof(block_handle_t));
                if (!blocks_list[blk_idx]) {
                    return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, blk_idx);
                }
            }

            block_handle_t *block = blocks_list[blk_idx];

            // Wypełnianie Konfiguracji
            block->cfg.block_idx  = blk_idx;
            block->cfg.block_type = data[1]; // Byte 1 = Block Type

            size_t offset = 5;
            block->cfg.in_cnt = data[offset++];
            memcpy(&block->cfg.in_connected, &data[offset], 2);
            offset += 2;
            block->cfg.q_cnt = data[offset++];

            // Alokacja tablic Wejść i Wyjść
            if (block->cfg.in_cnt > 0 && block->inputs == NULL) {
                block->inputs = (void**)calloc(block->cfg.in_cnt, sizeof(void*));
                if (!block->inputs) return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, blk_idx);
            }

            if (block->cfg.q_cnt > 0 && block->outputs == NULL) {
                block->outputs = (emu_mem_instance_iter_t*)calloc(block->cfg.q_cnt, sizeof(emu_mem_instance_iter_t));
                if (!block->outputs) return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, blk_idx);
            }

            // --- NOWOŚĆ: Parsowanie Wejść i Wyjść dla tego bloku ---
            
            // 1. Parsuj Referencje Wejść (Access Trees)
            if (block->cfg.in_cnt > 0) {
                emu_result_t res_in = emu_parse_block_inputs(source, block);
                if (res_in.code != EMU_OK) {
                    LOG_W(TAG, "Block %d: Inputs parsing issue: %d", blk_idx, res_in.code);
                }
            }

            // 2. Parsuj Definicje Wyjść (Direct Pointers)
            if (block->cfg.q_cnt > 0) {
                emu_result_t res_out = emu_parse_block_outputs(source, block);
                if (res_out.code != EMU_OK) {
                    LOG_W(TAG, "Block %d: Outputs parsing issue: %d", blk_idx, res_out.code);
                }
            }
            
            // 3. Parsuj Custom Data
            // Check bounds for function pointer array
            if (block->cfg.block_type < 255 && emu_block_parsers_table[block->cfg.block_type] != NULL) {
                LOG_I(TAG, "Parsing content for Block %d Type %d", blk_idx, block->cfg.block_type);
                emu_block_parsers_table[block->cfg.block_type](source, block);
            } else {
                LOG_W(TAG, "No parser for Block Type %d", block->cfg.block_type);
            }
            
            found_cnt++;
            LOG_D(TAG, "Configured Block %d (Type:%d In:%d Out:%d)", 
                  blk_idx, block->cfg.block_type, block->cfg.in_cnt, block->cfg.q_cnt);
        }
    }

    if (found_cnt < blocks_total_cnt) {
        LOG_W(TAG, "Configured %d blocks out of %d expected", found_cnt, blocks_total_cnt);
        return EMU_RESULT_OK(); 
    }
    return EMU_RESULT_OK();
}


bool emu_block_check_inputs_updated(block_handle_t *block) {
    if (!block) return false;

    for (uint8_t i = 0; i < block->cfg.in_cnt; i++) {
        if ((block->cfg.in_connected >> i) & 0x01) {
            mem_acces_s_t *access_node = (mem_acces_s_t*)block->inputs[i];
            
            if (unlikely(!access_node)) {
                return false;
            }

            emu_mem_t *mem = s_mem_contexts[access_node->reference_id];

            emu_mem_instance_iter_t meta;
            meta.raw = (uint8_t*)mem->instances[access_node->target_type][access_node->target_idx];
            
            if (unlikely((meta).raw == NULL)) return false; // Invalid instance

            if (meta.single->updated == 0) {
                return false;
            }
        }
    }
    return true;
}

emu_result_t emu_block_set_output(block_handle_t *block, emu_variable_t *var, uint8_t num) {
    if (unlikely(!block || !var)) {return EMU_RESULT_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, num);}
    
    // 1. Validate Output Index
    if (unlikely(num >= block->cfg.q_cnt)) {
        return EMU_RESULT_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, num);
    }

    // 2. Get Metadata Pointer (Directly from Block)
    // block->outputs is an array of unions, .single gives the struct ptr
    emu_mem_instance_s_t *meta = block->outputs[num].single;
    
    if (unlikely(!meta)) {return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, num);}

    // 3. Resolve Memory Context
    emu_mem_t *mem = s_mem_contexts[meta->reference_id];
    if (unlikely(!mem)) {return EMU_RESULT_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, num);}

    double src_val = emu_var_to_double(*var);

    double rnd = (meta->target_type < DATA_F) ? round(src_val) : src_val;

    uint32_t offset = meta->start_idx;

    void *pool_ptr = mem->data_pools[meta->target_type];
    
    if (unlikely(!pool_ptr)) {
        LOG_E(TAG, "Output Pool NULL Type %d", meta->target_type);
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, num);
    }

    switch (meta->target_type) {
        case DATA_UI8:  ((uint8_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); break;
        case DATA_UI16: ((uint16_t*)pool_ptr)[offset] = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); break;
        case DATA_UI32: ((uint32_t*)pool_ptr)[offset] = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); break;
        case DATA_I8:   ((int8_t*)pool_ptr)[offset]   = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); break;
        case DATA_I16:  ((int16_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); break;
        case DATA_I32:  ((int32_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); break;
        
        case DATA_F:    ((float*)pool_ptr)[offset]    = (float)src_val; break;
        case DATA_D:    ((double*)pool_ptr)[offset]   = src_val; break;
        case DATA_B:    ((bool*)pool_ptr)[offset]     = (src_val != 0.0); break;
        
        default: 
            return EMU_RESULT_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, num);
    }
    
    // 7. Mark as Updated
    meta->updated = 1;

    return EMU_RESULT_OK();
}

void emu_block_reset_outputs_status(block_handle_t *block) {
    if (unlikely(!block || block->cfg.q_cnt == 0 || !block->outputs)) {
        return;
    }

    for (uint8_t i = 0; i < block->cfg.q_cnt; i++) {
        emu_mem_instance_s_t *instance = block->outputs[i].single;
        if (likely(instance)){instance->updated = 0;}
    }
}