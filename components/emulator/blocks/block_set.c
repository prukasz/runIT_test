#include "block_set.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h" 
#include "emulator_blocks.h"
#include "esp_log.h"


static const char* TAG = __FILE_NAME__;

#define BLOCK_SET_TARGET   0
#define BLOCK_SET_VALUE    1


/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

emu_result_t block_set(block_handle_t *block) {
    emu_result_t res= EMU_RESULT_OK();
    if(!block_in_updated(block, BLOCK_SET_VALUE)){ EMU_RETURN_OK(EMU_LOG_block_inactive, EMU_OWNER_block_set, block->cfg.block_idx, TAG, "Block Disabled"); }
    emu_variable_t v_value;
    res = mem_get(block->inputs[BLOCK_SET_VALUE], false, &v_value);
    LOG_I(TAG, "value TYPE %d", v_value.type);
    
    res = mem_set(block->inputs[BLOCK_SET_TARGET], v_value);
    if(!res.code){LOG_I(TAG, "SET CODE %s", EMU_ERR_TO_STR(res.code));}
    return res;
}


