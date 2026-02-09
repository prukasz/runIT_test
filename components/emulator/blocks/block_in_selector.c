#include "block_in_selector.h"
#include "emu_blocks.h"
#include "string.h"
#include "emu_logging.h"

static const char *TAG = __FILE_NAME__;

#define BLOCK_IN_SEL_EN       0
#define BLOCK_IN_SEL_SEL      1
#define BLOCK_IN_SEL_OPT_BASE 2

#undef OWNER
#define OWNER EMU_OWNER_block_in_selector
emu_result_t block_in_selector(block_handle_t block){
    
    // Check EN input first (in_0)
    if (!block_check_in_true(block, BLOCK_IN_SEL_EN)) {RET_OK_INACTIVE(block->cfg.block_idx);}
    
    // Update only on selector change
    if (block_in_updated(block, BLOCK_IN_SEL_SEL)){
        // Fetch selector value
        uint8_t selector = 0; 
        MEM_GET(&selector, block->inputs[BLOCK_IN_SEL_SEL]);
        
        // Options count = total inputs - 2 (EN + SEL)
        uint8_t opt_count = block->cfg.in_cnt - BLOCK_IN_SEL_OPT_BASE;
        if (selector >= opt_count){RET_WD(EMU_ERR_BLOCK_SELECTOR_OOB, block->cfg.block_idx, 0, "[%"PRIu16"], Selector [%d] > options [%d]", block->cfg.block_idx, selector, opt_count);}
        
        // Mimic output struct from selected option
        *block->outputs[0]->instance = *block->inputs[BLOCK_IN_SEL_OPT_BASE + selector]->instance;
    }
    block->outputs[0]->instance->updated = 1; // Mark output as updated so it can trigger next blocks
    return EMU_RESULT_OK();
}


