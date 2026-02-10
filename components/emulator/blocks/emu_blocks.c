#include "emu_blocks.h"
#include "blocks_functions_list.h"
#include "emu_helpers.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER EMU_OWNER_emu_parse_block
emu_result_t emu_block_parse_create_list(const uint8_t *data, const uint16_t data_len, void * emu_code_handle){
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    uint16_t count = parse_get_u16(data, 0);
    size_t ptr_list_size = count * sizeof(block_handle_t);
    size_t data_pool_size = count * sizeof(block_data_t);

    //blob to store both pointers and data pool at onec
    void *blobs = calloc(1, ptr_list_size + data_pool_size);
    if(!blobs){RET_E(EMU_ERR_NO_MEM, "Not enough memory to create block list with %d blocks", count);}

    code->blocks_list = (block_handle_t*)blobs;
    block_data_t *block_pool = (block_data_t*)((uint8_t*)blobs + ptr_list_size);
    for (uint16_t i = 0; i < count; i++) {
        // Set the i-th pointer to point to the i-th block in the pool
        code->blocks_list[i] = &block_pool[i];
    }
    code->total_blocks = count;
    LOG_I(TAG, "Created block list with %d blocks", count);
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_parse_cfg
emu_result_t emu_block_parse_cfg(const uint8_t *data, const uint16_t data_len, void * emu_code_handle){
    LOG_I(TAG, "Parsing block configuration.....");
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    block_data_t block_tmp = {0};

    memcpy(&block_tmp.cfg, data, sizeof(block_tmp.cfg));

    uint16_t idx = block_tmp.cfg.block_idx;

    if (idx>code->total_blocks){RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, idx, 0, "No space for block with index %d", idx);}
    block_handle_t block = code->blocks_list[idx];
    
    *block = block_tmp;
    block -> inputs = (mem_access_t**)calloc(block_tmp.cfg.in_cnt, sizeof(mem_access_t*));
    block -> outputs = (mem_access_t**)calloc(block_tmp.cfg.q_cnt, sizeof(mem_access_t*));
    if((!block->inputs && block_tmp.cfg.in_cnt > 0) || (!block->outputs && block_tmp.cfg.q_cnt > 0)){RET_ED(EMU_ERR_NO_MEM, idx, 0, "Not enough memory for block %d inputs/outputs", idx);}
    LOG_I(TAG, "Parsed block cfg idx %d type %d (in:%d out:%d)", idx, block->cfg.block_type, block->cfg.in_cnt, block->cfg.q_cnt);
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_block_parse_input
emu_result_t emu_block_parse_input(const uint8_t *data, const uint16_t data_len, void * emu_code_handle){
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    uint16_t block_idx = parse_get_u16(data, 0);
    uint8_t in_idx = data[2];
    mem_access_t *access = NULL;
    uint16_t parse_idx = 3;
    uint16_t index = 0;
    emu_err_t res_code = emu_mem_parse_access(data + parse_idx, data_len - parse_idx, &index, &access);
    if(res_code != EMU_OK){RET_ED(res_code, block_idx, 0, "Failed to parse input access for block %d input %d, error %s", block_idx, in_idx, EMU_ERR_TO_STR(res_code));}
    if(block_idx >= code->total_blocks){RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Invalid block idx %d for input parse", block_idx);}
    block_handle_t block = code->blocks_list[block_idx];
    if(in_idx >= block->cfg.in_cnt){RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Invalid input idx %d for block %d", in_idx, block_idx);}
    block->inputs[in_idx] = access;
    return EMU_RESULT_OK();
}


#undef OWNER
#define OWNER EMU_OWNER_emu_block_parse_output
emu_result_t emu_block_parse_output(const uint8_t *data, const uint16_t data_len, void * emu_code_handle){
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    uint16_t block_idx = parse_get_u16(data, 0);
    uint8_t q_idx = data[2];
    mem_access_t *access = NULL;
    uint16_t parse_idx = 3;
    uint16_t index = 0;
    emu_err_t res_code = emu_mem_parse_access(data + parse_idx, data_len - parse_idx, &index, &access);
    if(res_code != EMU_OK){RET_ED(res_code, block_idx, 0, "Failed to parse output access for block %d output %d", block_idx, q_idx);}
    if(block_idx >= code->total_blocks){RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Invalid block idx %d for output parse", block_idx);}
    block_handle_t block = code->blocks_list[block_idx];
    if(q_idx >= block->cfg.q_cnt){RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Invalid output idx %d for block %d", q_idx, block_idx);}
    block->outputs[q_idx] = access;
    return EMU_RESULT_OK();
}

void block_free(block_handle_t block){
    if(block){
        if(block->inputs){free(block->inputs);}
        if(block->outputs){free(block->outputs);}
    }
    //add free with already created 
}

/**
 * @brief Free all blocks in code handle
 */
void emu_blocks_free_all(void* emu_code_handle){
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    if(code->blocks_list){
        for(uint16_t i=0;i<code->total_blocks;i++){
            block_free(code->blocks_list[i]);
        }
        free(code->blocks_list);
        code->blocks_list = NULL;
    }
    code->total_blocks = 0;
}
