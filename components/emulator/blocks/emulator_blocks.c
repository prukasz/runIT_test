#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_variables.h"
#include "blocks_functions_list.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

extern emu_mem_t* s_mem_contexts[];
static const char* TAG = "EMU_BLOCKS";

/* ============================================================================
    ZARZĄDZANIE PAMIĘCIĄ BLOKÓW (FREE)
   ============================================================================ */

static void _free_single_block(block_handle_t* block) {
    if (!block) return;
    
    if (block->inputs) {
        free(block->inputs);
        block->inputs = NULL;
    }

    if (block->outputs) {
        free(block->outputs);
        block->outputs = NULL;
    }

    if (block->custom_data) {
        emu_block_free_func free_func = emu_block_free_table[block->cfg.block_type];
        if (free_func){free_func(block);}
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
            _free_single_block(blocks_list[i]);
            blocks_list[i] = NULL;
        }
    }
    
    free(blocks_list);
    LOG_I(TAG, "All %d blocks freed", blocks_cnt);
}

/****************************************************************************
 * TOTAL BLOCKS CNT
 ****************************************************************************/

emu_result_t emu_parse_blocks_total_cnt(chr_msg_buffer_t *source, block_handle_t ***blocks_list, uint16_t *blocks_total_cnt) {
    if (!source || !blocks_list || !blocks_total_cnt) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0xFFFF, TAG, "Null ptr provided");
    }

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (len < 4) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == EMU_H_BLOCK_CNT) {
            uint16_t cnt = 0;
            memcpy(&cnt, &data[2], 2);

            if (cnt == 0) {
                EMU_RETURN_NOTICE(EMU_OK, 0xFFFF, TAG, "Block count is 0");
            }

            *blocks_list = (block_handle_t**)calloc(cnt, sizeof(block_handle_t*));
            
            if (*blocks_list == NULL) {
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, 0, TAG, "Failed to allocate list for %d blocks", cnt);
            }

            *blocks_total_cnt = cnt;
            LOG_I(TAG, "Allocated list for %d blocks", cnt);
            return EMU_RESULT_OK();
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_H_BLOCK_CNT, TAG, "Block count header not found");
}

/**********************************************************************************
 * SINGLE BLOCK PARSE
 *********************************************************************************/

emu_result_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t **blocks_list, uint16_t blocks_total_cnt) {
    if (!blocks_list || blocks_total_cnt == 0) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "Null ptr provided");
    }

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint16_t found_cnt = 0;

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Header Check: [BB] [Type] [Subtype=00] [ID:2] ... Payload
        if (len < 9) continue;

        if (data[0] == EMU_H_BLOCK_START && data[2] == EMU_H_BLOCK_PACKET_CFG) {
            
            uint16_t blk_idx = 0;
            memcpy(&blk_idx, &data[3], 2);

            if (blk_idx >= blocks_total_cnt) {
                EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, i, TAG, "Block ID %d exceeds total %d", blk_idx, blocks_total_cnt);
            }

            if (blocks_list[blk_idx] == NULL) {
                blocks_list[blk_idx] = (block_handle_t*)calloc(1, sizeof(block_handle_t));
                if (!blocks_list[blk_idx]) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, blk_idx, TAG, "Block struct alloc failed");
                }
            }

            block_handle_t *block = blocks_list[blk_idx];
            block->cfg.block_idx  = blk_idx;
            block->cfg.block_type = data[1]; 

            size_t offset = 5;
            block->cfg.in_cnt = data[offset++];
            memcpy(&block->cfg.in_connected, &data[offset], 2);
            offset += 2;
            block->cfg.q_cnt = data[offset++];

            // Alokacja tablic Wejść i Wyjść
            if (block->cfg.in_cnt > 0 && block->inputs == NULL) {
                block->inputs = (void**)calloc(block->cfg.in_cnt, sizeof(void*));
                if (!block->inputs) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, blk_idx, TAG, "Inputs alloc failed");
            }

            if (block->cfg.q_cnt > 0 && block->outputs == NULL) {
                block->outputs = (emu_mem_instance_iter_t*)calloc(block->cfg.q_cnt, sizeof(emu_mem_instance_iter_t));
                if (!block->outputs) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, blk_idx, TAG, "Outputs alloc failed");
            }

            // Inputs
            if (block->cfg.in_cnt > 0) {
                emu_result_t res_in = emu_parse_block_inputs(source, block);
                if (res_in.code != EMU_OK) {
                    EMU_RETURN_CRITICAL(res_in.code, blk_idx, TAG, "Block %d: Inputs parsing issue: %d", blk_idx, res_in.code);
                }
            }

            // Outputs
            if (block->cfg.q_cnt > 0) {
                emu_result_t res_out = emu_parse_block_outputs(source, block);
                if (res_out.code != EMU_OK) {
                    EMU_RETURN_CRITICAL(res_out.code, blk_idx, TAG, "Block %d: Outputs parsing issue: %d", blk_idx, res_out.code);
                }
            }
            
            // Custom Data Parsing
            if (block->cfg.block_type < UINT8_MAX && emu_block_parsers_table[block->cfg.block_type] != NULL) {
                LOG_D(TAG, "Parsing content for Block %d Type %d", blk_idx, block->cfg.block_type);
                emu_block_parsers_table[block->cfg.block_type](source, block);
            } else {
                LOG_W(TAG, "No parser for Block Type %d", block->cfg.block_type);
            }
            
            found_cnt++;
            LOG_I(TAG, "Configured Block %d (Type:%d In:%d Out:%d)", blk_idx, block->cfg.block_type, block->cfg.in_cnt, block->cfg.q_cnt);
        }
    }

    if (found_cnt < blocks_total_cnt) {
        LOG_D(TAG, "Configured %d blocks out of %d expected", found_cnt, blocks_total_cnt);
        return EMU_RESULT_OK(); 
    }
    return EMU_RESULT_OK();
}

emu_result_t emu_parse_blocks_verify_all(block_handle_t **blocks_list, uint16_t blocks_total_cnt) {
    if (!blocks_list || blocks_total_cnt == 0) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "Null ptr provided or zero blocks to check");
    }
    
    for (size_t i = 0; i < blocks_total_cnt; i++) {
        block_handle_t *block = blocks_list[i];

        if (!block) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, i, TAG, "Block %d is NULL", i);}

        if (block->cfg.block_idx != i) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, i, TAG, "Block idx mismatch %d != %d", block->cfg.block_idx, i);}
        //inputs
        if (block->cfg.in_cnt > 0) {
            if (!block->inputs) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, i, TAG, "Block %d has inputs count %d but input array is NULL", (int)i, block->cfg.in_cnt);}

            for (uint8_t in_idx = 0; in_idx < block->cfg.in_cnt; in_idx++) {
                
                bool is_connected = (block->cfg.in_connected >> in_idx) & 0x01;
                
                if (is_connected) {
                    if (block->inputs[in_idx] == NULL) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, i, TAG, "Block %d Input %d marked connected but pointer is NULL", (int)i, in_idx);}
                }
            }
        }

        //outputs
        if (block->cfg.q_cnt > 0) {
            if (!block->outputs) {
                EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, i, TAG, "Block %d has outputs count %d but output array is NULL", (int)i, block->cfg.q_cnt);
            }

            for (uint8_t q_idx = 0; q_idx < block->cfg.q_cnt; q_idx++) {
                if (block->outputs[q_idx].raw == NULL) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, i, TAG, "Block %d Output %d is NULL (not linked to memory)", (int)i, q_idx);
                }
            }
        
        }
        //verify content
        emu_block_verify_func verify_func = emu_block_verify_table[block->cfg.block_type];
        emu_result_t res = EMU_RESULT_OK();
        if (verify_func) {
            res = verify_func(block);
        }
        if (res.code != EMU_OK && res.abort){EMU_RETURN_CRITICAL(res.code, i, TAG, "Block content verify failed for block %d", i);}
    }
    return EMU_RESULT_OK();
}