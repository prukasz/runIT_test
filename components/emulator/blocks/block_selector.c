#include "block_selector.h"
#include "emulator_blocks.h"
#include "string.h"
#include "emulator_logging.h"

static const char *TAG = __FILE_NAME__;

typedef struct {
    uint8_t out_cnt;
    void** out_refs;
    uint8_t last_updated;
} block_selector_cfg_t;

emu_result_t block_selector(block_handle_t *block){
    uint8_t sel_index = 0;
    MEM_GET(&sel_index , block->inputs[0]);
    LOG_I(TAG, "Selector index: %d", sel_index);
    block_selector_cfg_t *cfg = (block_selector_cfg_t*)block->custom_data;
    if (sel_index >= cfg->out_cnt){
        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_SELECTOR_OOB, EMU_OWNER_block_selector, block->cfg.block_idx, 0, TAG,
                            "Selector OOB: sel_index=%d, out_cnt=%d", sel_index, cfg->out_cnt);
    }
    
    mem_access_s_t* out_access = (mem_access_s_t*)block->outputs[0];
    mem_access_s_t* sel_access = (mem_access_s_t*)cfg->out_refs[sel_index];
    LOG_I(TAG, "check1");
    // Cast to actual instances
    emu_mem_instance_arr_t* out_inst = (emu_mem_instance_arr_t*)out_access->instance;
    emu_mem_instance_arr_t* sel_inst = (emu_mem_instance_arr_t*)sel_access->instance;
    LOG_I(TAG, "check2");
    // Copy instance metadata and data pointer from selected instance
    out_inst->context_id = sel_inst->context_id;
    LOG_I(TAG, "Selector selected context: %d", sel_inst->context_id);
    out_inst->target_type = sel_inst->target_type;
    LOG_I(TAG, "Selector selected type: %d", sel_inst->target_type);
    out_inst->dims_cnt = sel_inst->dims_cnt;
    LOG_I(TAG, "Selector selected dims: %d", sel_inst->dims_cnt);
    out_inst->updated = sel_inst->updated;
    if(sel_inst->dims_cnt > 0){
        for (uint8_t i = 0; i < sel_inst->dims_cnt && i < 3; i++) {
            out_inst->dim_sizes[i] = sel_inst->dim_sizes[i];
        }
    }
    // Reference the selected instance's data pointer
    out_inst->data = sel_inst->data;
    out_access->is_resolved = true;
    
    cfg->last_updated = sel_index;
    EMU_RETURN_OK(EMU_LOG_block_selector_executed, EMU_OWNER_block_selector, block->cfg.block_idx, TAG,
                  "Selector executed: sel_index=%d", sel_index);
}

emu_result_t block_selector_parse(chr_msg_buffer_t *source, block_handle_t *block){
    // Allocate configuration structure
    block->custom_data = calloc(1, sizeof(block_selector_cfg_t));
    if (!block->custom_data){
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_selector_parse, block->cfg.block_idx, 0, TAG, "No mem for selector config");
    }
    
    block_selector_cfg_t *cfg = (block_selector_cfg_t*)block->custom_data;
    uint8_t *data;
    size_t len;
    uint16_t block_idx = block->cfg.block_idx;
    size_t buff_size = chr_msg_buffer_size(source);
    
    // Initialize counters
    cfg->out_cnt = 0;
    cfg->last_updated = 0xFF; // Invalid initial value
    
    // First pass: count output references
    uint8_t ref_count = 0;
    for (size_t search_idx = 0; search_idx < buff_size; search_idx++) {
        chr_msg_buffer_get(source, search_idx, &data, &len);
        
        if (len > 4 && data[0] == 0xBB && data[1] == BLOCK_SELECTOR && data[2] >= 0xB0 && data[2] <= 0xBF) {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);
            if (packet_blk_id == block_idx) {
                ref_count++;
            }
        }
    }
    
    if (ref_count == 0) {
        free(block->custom_data);
        block->custom_data = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_block_selector_parse, block_idx, 0, TAG, "Selector has no output references");
    }
    
    // Allocate output references array
    cfg->out_refs = calloc(ref_count, sizeof(void*));
    if (!cfg->out_refs) {
        free(block->custom_data);
        block->custom_data = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_block_selector_parse, block_idx, 0, TAG, "No mem for selector refs array");
    }
    
    // Second pass: parse output references
    uint8_t parsed_count = 0;
    for (size_t search_idx = 0; search_idx < buff_size; search_idx++) {
        chr_msg_buffer_get(source, search_idx, &data, &len);
        
        if (len > 4 && data[0] == 0xBB && data[1] == BLOCK_SELECTOR && data[2] >= 0xB0 && data[2] <= 0xBF) {
            uint16_t packet_blk_id;
            memcpy(&packet_blk_id, &data[3], 2);
            
            if (packet_blk_id == block_idx) {
                uint8_t output_idx = data[2] - 0xB0;
                
                if (output_idx >= ref_count) {
                    for (uint8_t i = 0; i < parsed_count; i++) {
                        // Free already parsed refs - implement mem_access_free if needed
                    }
                    free(cfg->out_refs);
                    free(block->custom_data);
                    block->custom_data = NULL;
                    EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_block_selector_parse, block_idx, 0, TAG,
                                       "Output index %d exceeds ref_count %d", output_idx, ref_count);
                }
                
                uint8_t *ptr = &data[5];
                size_t pl_len = len - 5;
                
                emu_result_t res = mem_access_parse_node_recursive(&ptr, &pl_len, &cfg->out_refs[output_idx]);
                if (res.abort) {
                    for (uint8_t i = 0; i < parsed_count; i++) {
                        // Free already parsed refs
                    }
                    free(cfg->out_refs);
                    free(block->custom_data);
                    block->custom_data = NULL;
                    EMU_RETURN_CRITICAL(res.code, EMU_OWNER_block_selector_parse, block_idx, ++res.depth, TAG,
                                       "Failed to parse output ref %d", output_idx);
                }
                parsed_count++;
            }
        }
    }
    
    cfg->out_cnt = ref_count;
    
    // Parse input (selector signal)
    chr_msg_buffer_get(source, block_idx, &data, &len);
    if (len < 5) {
        for (uint8_t i = 0; i < cfg->out_cnt; i++) {
            // Free parsed refs
        }
        free(cfg->out_refs);
        free(block->custom_data);
        block->custom_data = NULL;
        EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_block_selector_parse, block_idx, 0, TAG, "Selector input incomplete");
    }
    
    block->cfg.in_cnt = 1;
    block->cfg.q_cnt = 1;
    
    EMU_RETURN_OK(EMU_LOG_block_selector_parsed, EMU_OWNER_block_selector_parse, block_idx, TAG,
                  "Selector parsed: %d output refs", cfg->out_cnt);
}

void block_selector_free(block_handle_t* block){
    if(block && block->custom_data){
        block_selector_cfg_t* cfg = (block_selector_cfg_t*)block->custom_data;
        
        // Free output references array
        if(cfg->out_refs){
            free(cfg->out_refs);
            cfg->out_refs = NULL;
        }
        
        // Free config structure
        free(block->custom_data);
        block->custom_data = NULL;
        
        EMU_REPORT(EMU_LOG_block_selector_freed, EMU_OWNER_block_selector_free, block->cfg.block_idx, TAG, 
                   "Selector block memory freed");
    }
}

emu_result_t block_selector_verify(block_handle_t *block) {
    if (!block->custom_data) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_selector_verify, block->cfg.block_idx, 0, TAG, 
                           "Custom Data is NULL");
    }

    block_selector_cfg_t *cfg = (block_selector_cfg_t*)block->custom_data;

    if (cfg->out_cnt == 0) {
        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_selector_verify, block->cfg.block_idx, 0, TAG, 
                           "Selector has no output references");
    }

    if (!cfg->out_refs) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_selector_verify, block->cfg.block_idx, 0, TAG, 
                           "Output references array is NULL");
    }
    
    // Verify all output references are valid
    for (uint8_t i = 0; i < cfg->out_cnt; i++) {
        if (!cfg->out_refs[i]) {
            EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_selector_verify, block->cfg.block_idx, 0, TAG, 
                               "Output reference %d is NULL", i);
        }
    }
    
    // Verify input (selector signal)
    if (!block->inputs[0]) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_block_selector_verify, block->cfg.block_idx, 0, TAG, 
                           "Selector input is NULL");
    }
    
    EMU_RETURN_OK(EMU_LOG_block_selector_verified, EMU_OWNER_block_selector_verify, block->cfg.block_idx, TAG, 
                  "Selector verified: %d output refs", cfg->out_cnt);
}

