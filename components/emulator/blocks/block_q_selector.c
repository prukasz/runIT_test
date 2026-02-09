#include "block_q_selector.h"
#include "emu_blocks.h"
#include "string.h"
#include "emu_logging.h"
#include "emu_variables_acces.h"

static const char *TAG = __FILE_NAME__;

#define BLOCK_Q_SEL_EN  0
#define BLOCK_Q_SEL_SEL 1

#undef OWNER
#define OWNER EMU_OWNER_block_q_selector
emu_result_t block_q_selector(block_handle_t block){
    
    // Check EN input first (in_0)
    if (!block_check_in_true(block, BLOCK_Q_SEL_EN)) {
        for (uint8_t i = 0; i < block->cfg.q_cnt; i++){
            *block->outputs[i]->instance->data.b = false;
            block->outputs[i]->instance->updated = 0;
        }
        RET_OK_INACTIVE(block->cfg.block_idx);}
    
    // Update only on selector change
    if (block_in_updated(block, BLOCK_Q_SEL_SEL)){
        // Fetch selector value
        uint8_t selector = 0; 
        MEM_GET(&selector, block->inputs[BLOCK_Q_SEL_SEL]);
        for (uint8_t i = 0; i < block->cfg.q_cnt; i++){
            *block->outputs[i]->instance->data.b = false;
            block->outputs[i]->instance->updated = 0;
        }
        // Check bounds
        if (selector >= block->cfg.q_cnt){
            RET_ED(EMU_ERR_BLOCK_SELECTOR_OOB, block->cfg.block_idx, 0, "[%"PRIu16"] Selector value %u out of bounds (max %u)", block->cfg.block_idx, selector, block->cfg.q_cnt-1);
        }
        
        // Mark ONLY the selected output as true and updated
        *block->outputs[selector]->instance->data.b = true;
        block->outputs[selector]->instance->updated = 1;
    }
    
    return EMU_RESULT_OK();
}
