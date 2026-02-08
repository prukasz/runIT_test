#include "block_in_selector.h"
#include "emu_blocks.h"
#include "string.h"
#include "emu_logging.h"

static const char *TAG = __FILE_NAME__;


#undef OWNER
#define OWNER EMU_OWNER_block_in_selector
emu_result_t block_in_selector(block_handle_t block){
    
    //update only on change
    if (block_in_updated(block, 0)){
        //first fetch selector 
        uint8_t selector = 0; 
        MEM_GET(&selector, block->inputs[0]);
        if (selector >= block->cfg.in_cnt){RET_WD(EMU_ERR_BLOCK_SELECTOR_OOB, block->cfg.block_idx, 0, "["PRIu16"], Selector [%d] > inputs [%d]", selector, block->cfg.in_cnt);}
        //then mimic output struct
        *block->outputs[0]->instance = *block->inputs[selector+1]->instance;
    }
    block->outputs[0]->instance->updated = 1; //mark output as updated so it can trigger next blocks
    //else do nothing so value remains
    return EMU_RESULT_OK();
}


