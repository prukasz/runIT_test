#include "emulator_blocks.h"
#include "blocks_functions_list.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

extern emu_mem_t* s_mem_contexts[];
static const char* TAG = __FILE_NAME__;

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

void emu_blocks_free_all(emu_code_handle_t code) {

    for (size_t i = 0; i < code->total_blocks; i++) {
        if (code->blocks_list[i]) {
            _free_single_block(code->blocks_list[i]);
            code->blocks_list[i] = NULL;
        }
    }

    
    free(code->blocks_list);
    code->total_blocks = 0;
    LOG_I(TAG, "All %d blocks freed", code->total_blocks);
}

/****************************************************************************
 * TOTAL BLOCKS CNT
 ****************************************************************************/

emu_result_t emu_parse_blocks_total_cnt(chr_msg_buffer_t *source, emu_code_handle_t code) {

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
                EMU_RETURN_NOTICE(EMU_OK, EMU_OWNER_emu_parse_blocks_total_cnt, 0xFFFF, 0, TAG, "Block count is 0");
            }

            code->blocks_list = (block_handle_t**)calloc(cnt, sizeof(block_handle_t*));
            
            if (code->blocks_list == NULL) {
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_parse_blocks_total_cnt, 0, 0, TAG, "Failed to allocate list for %d blocks", cnt);
            }

            code->total_blocks = cnt;
            LOG_I(TAG, "Allocated list for %d blocks", cnt);
            EMU_RETURN_OK(EMU_LOG_blocks_list_allocated, EMU_OWNER_emu_parse_blocks_total_cnt, 0, TAG, "Block count header found: %d", cnt);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_parse_blocks_total_cnt, EMU_H_BLOCK_CNT, 0, TAG, "Block count header not found");
}

/**********************************************************************************
 * SINGLE BLOCK PARSE
 *********************************************************************************/

emu_result_t emu_parse_block(chr_msg_buffer_t *source, emu_code_handle_t code) {

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

            if (blk_idx >= code->total_blocks) {
                EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Block ID %d exceeds total %d", blk_idx, code->total_blocks);
            }

            if (code->blocks_list[blk_idx] == NULL) {
                code->blocks_list[blk_idx] = (block_handle_t*)calloc(1, sizeof(block_handle_t));
                if (!code->blocks_list[blk_idx]) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Block struct alloc failed");
                }
            }

            block_handle_t *block = code->blocks_list[blk_idx];
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
                if (!block->inputs) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Inputs alloc failed");
            }

            if (block->cfg.q_cnt > 0 && block->outputs == NULL) {
                block->outputs = (void**)calloc(block->cfg.q_cnt, sizeof(void*));
                if (!block->outputs) EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Outputs alloc failed");
            }

            // Inputs
            if (block->cfg.in_cnt > 0) {
                emu_result_t res_in = emu_parse_block_inputs(source, block);
                if (res_in.code != EMU_OK) {
                    EMU_RETURN_CRITICAL(res_in.code, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Block %d: Inputs parsing issue: %d", blk_idx, res_in.code);
                }
            }

            // Outputs
            if (block->cfg.q_cnt > 0) {
                emu_result_t res_out = emu_parse_block_outputs(source, block);
                if (res_out.code != EMU_OK) {
                    EMU_RETURN_CRITICAL(res_out.code, EMU_OWNER_emu_parse_block, blk_idx, 0, TAG, "Block %d: Outputs parsing issue: %d", blk_idx, res_out.code);
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

    if (found_cnt < code->total_blocks) {
        LOG_D(TAG, "Configured %d blocks out of %d expected", found_cnt, code->total_blocks);
        EMU_RETURN_NOTICE(EMU_OK, EMU_OWNER_emu_parse_block, 0, 0, TAG, "Configured %d blocks out of %d expected", found_cnt, code->total_blocks);
    }
    EMU_RETURN_OK(EMU_LOG_blocks_parsed_once, EMU_OWNER_emu_parse_block, 0, TAG, "All %d blocks parsed", found_cnt);
}

emu_result_t emu_parse_blocks_verify_all(emu_code_handle_t code) {

    for (size_t i = 0; i < code->total_blocks; i++) {
        block_handle_t *block = code->blocks_list[i];

        if (!block) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block %d is NULL", i);}

        if (block->cfg.block_idx != i) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block idx mismatch %d != %d", block->cfg.block_idx, i);}
        //inputs
        if (block->cfg.in_cnt > 0) {
            if (!block->inputs) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block %d has inputs count %d but input array is NULL", (int)i, block->cfg.in_cnt);}

            for (uint8_t in_idx = 0; in_idx < block->cfg.in_cnt; in_idx++) {
                
                bool is_connected = (block->cfg.in_connected >> in_idx) & 0x01;
                
                if (is_connected) {
                    if (block->inputs[in_idx] == NULL) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block %d Input %d marked connected but pointer is NULL", (int)i, in_idx);}
                }
            }
        }

        //outputs
        if (block->cfg.q_cnt > 0) {
            if (!block->outputs) {
                EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block %d has outputs count %d but output array is NULL", (int)i, block->cfg.q_cnt);
            }

            for (uint8_t q_idx = 0; q_idx < block->cfg.q_cnt; q_idx++) {
                if (block->outputs[q_idx] == NULL) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block %d Output %d is NULL (not linked to memory)", (int)i, q_idx);
                }
            }
        
        }
        //verify content
        emu_block_verify_func verify_func = emu_block_verify_table[block->cfg.block_type];
        emu_result_t res = {.code = EMU_OK};
        if (verify_func) {
            res = verify_func(block);
        }
        if (res.code != EMU_OK && res.abort){EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_blocks_verify_all, i, 0, TAG, "Block content verify failed for block %d", i);}
    }
    EMU_RETURN_OK(EMU_LOG_blocks_verified, EMU_OWNER_emu_parse_blocks_verify_all, 0, TAG, "All %d blocks verified", code->total_blocks);
}

emu_result_t emu_parse_block_inputs(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data; size_t len, b_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < b_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len > 5 && data[0] == 0xBB && data[2] >= 0xF0) {
            uint16_t b_idx; memcpy(&b_idx, &data[3], 2);
            if (b_idx == block->cfg.block_idx){
                uint8_t r_idx = data[2] - 0xF0;
                if (r_idx < block->cfg.in_cnt) {
                    uint8_t *ptr = &data[5]; size_t pl_len = len - 5;
                    LOG_I(TAG, "Parsing Input %d for block %d", r_idx, b_idx);
                    
                    emu_result_t res = mem_access_parse_node_recursive(&ptr, &pl_len, &block->inputs[r_idx]);
                    if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_block_inputs, i, 0, TAG, "Parse Failed Block %d Input %d: %s", b_idx, r_idx, EMU_ERR_TO_STR(res.code));}
                }
            }
        }
    }
    EMU_RETURN_OK(EMU_LOG_parsed_block_inputs, EMU_OWNER_emu_parse_block_inputs, 0, TAG, "Parsed inputs for block %d", block->cfg.block_idx);
}

emu_result_t emu_parse_block_outputs(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data; size_t len, b_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < b_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len > 5 && data[0] == 0xBB && data[2] >= 0xE0) {
            uint16_t b_idx; memcpy(&b_idx, &data[3], 2);
            if (b_idx == block->cfg.block_idx){
                uint8_t r_idx = data[2] - 0xE0;
                if (r_idx < block->cfg.q_cnt) {
                    uint8_t *ptr = &data[5]; size_t pl_len = len - 5;
                    LOG_I(TAG, "Parsing Input %d for block %d", r_idx, b_idx);
                    
                    emu_result_t res = mem_access_parse_node_recursive(&ptr, &pl_len, &block->outputs[r_idx]);
                    if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_block_outputs, i, 0, TAG, "Parse Failed Block %d Input %d: %s", b_idx, r_idx, EMU_ERR_TO_STR(res.code));}
                }
            }
        }
    }
    EMU_RETURN_OK(EMU_LOG_parsed_block_outputs, EMU_OWNER_emu_parse_block_outputs, 0, TAG, "Parsed outputs for block %d", block->cfg.block_idx);
}