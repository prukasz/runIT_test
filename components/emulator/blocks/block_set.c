#include "block_set.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h" 
#include "emulator_blocks.h"
#include "esp_log.h"


static const char* TAG = __FILE_NAME__;

#define BLOCK_SET_VALUE    0
#define BLOCK_SET_TARGET   1

//we use only VALUE and TARGET inputs no enable input as we check is VALUE updated

#undef OWNER
#define OWNER EMU_OWNER_block_set
emu_result_t block_set(block_handle_t block) {
    emu_result_t res= EMU_RESULT_OK();
    if(!block_in_updated(block, BLOCK_SET_VALUE)){ RET_OKD(block->cfg.block_idx, "Block Disabled"); }

    //get value from input 0 to be set  
    mem_var_t v_value;
    res = mem_get(&v_value, (const mem_access_t*)block->inputs[BLOCK_SET_VALUE], false);

    if(res.code != EMU_OK){RET_ED(res.code, block->cfg.block_idx, ++res.depth, "Failed to get value to set: %s", EMU_ERR_TO_STR(res.code));}
    
    //Use mem_set to set value to target
    res = mem_set(v_value, (const mem_access_t*)block->inputs[BLOCK_SET_TARGET]);

    if(res.code != EMU_OK){RET_ED(res.code, block->cfg.block_idx, ++res.depth, "Failed to set value: %s", EMU_ERR_TO_STR(res.code));}
    return res;
}


