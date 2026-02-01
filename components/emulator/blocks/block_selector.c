#include "block_selector.h"
#include "emu_blocks.h"
#include "string.h"
#include "emu_logging.h"

static const char *TAG = __FILE_NAME__;

typedef struct {
    uint8_t out_cnt;
    void** out_refs;
    uint8_t last_updated;
} block_selector_cfg_t;

#undef OWNER
#define OWNER EMU_OWNER_block_selector
emu_result_t block_selector(block_handle_t block){
    uint8_t sel_index = 0;
    MEM_GET(&sel_index , block->inputs[0]);
    LOG_I(TAG, "Selector index: %d", sel_index);
    block_selector_cfg_t *cfg = (block_selector_cfg_t*)block->custom_data;
    if (sel_index >= cfg->out_cnt){
        RET_ED(EMU_ERR_BLOCK_SELECTOR_OOB, block->cfg.block_idx, 0,
                            "Selector OOB: sel_index=%d, out_cnt=%d", sel_index, cfg->out_cnt);
    }
    
    mem_access_t* out_access = (mem_access_t*)block->outputs[0];
    mem_access_t* sel_access = (mem_access_t*)cfg->out_refs[sel_index];
    LOG_I(TAG, "check1");
    // Cast to actual instances
    mem_instance_t* out_inst = out_access->instance;
    mem_instance_t* sel_inst = sel_access->instance;
    LOG_I(TAG, "check2");
    // Copy instance metadata and data pointer from selected instance
    out_inst->context = sel_inst->context;
    LOG_I(TAG, "Selector selected context: %d", sel_inst->context);
    out_inst->type = sel_inst->type;
    LOG_I(TAG, "Selector selected type: %d", sel_inst->type);
    out_inst->dims_cnt = sel_inst->dims_cnt;
    LOG_I(TAG, "Selector selected dims: %d", sel_inst->dims_cnt);
    out_inst->updated = sel_inst->updated;
    out_inst->dims_idx = sel_inst->dims_idx;
    // Reference the selected instance's data pointer
    out_inst->data = sel_inst->data;
    out_access->is_index_resolved = true;
    
    cfg->last_updated = sel_index;
    RET_OKD(block->cfg.block_idx, "Selector executed: sel_index=%d", sel_index);
}

#undef OWNER
#define OWNER EMU_OWNER_block_selector_parse
emu_result_t block_selector_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr){
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    // For now selector doesn't have custom data packets - configuration is done via inputs/outputs
    // This is a placeholder for future selector-specific data
    LOG_I(TAG, "Selector parse called - no custom data required");
    
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_block_selector_free
void block_selector_free(block_handle_t block){
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
        
        REP_ND(EMU_LOG_block_selector_freed, block->cfg.block_idx, 0, 
                   "Selector block memory freed");
    }
}

#undef OWNER
#define OWNER EMU_OWNER_block_selector_verify
emu_result_t block_selector_verify(block_handle_t block) {
    if (!block->custom_data) {
        RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, 
                           "Custom Data is NULL");
    }

    block_selector_cfg_t *cfg = (block_selector_cfg_t*)block->custom_data;

    if (cfg->out_cnt == 0) {
        RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, 
                           "Selector has no output references");
    }

    if (!cfg->out_refs) {
        RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, 
                           "Output references array is NULL");
    }
    
    // Verify all output references are valid
    for (uint8_t i = 0; i < cfg->out_cnt; i++) {
        if (!cfg->out_refs[i]) {
            RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, 
                               "Output reference %d is NULL", i);
        }
    }
    
    // Verify input (selector signal)
    if (!block->inputs[0]) {
        RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, 
                           "Selector input is NULL");
    }
    
    RET_OKD(block->cfg.block_idx, "Selector verified: %d output refs", cfg->out_cnt);
}


