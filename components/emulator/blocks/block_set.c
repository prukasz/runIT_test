#include "block_set.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h" 
#include "emulator_blocks.h"
#include "esp_log.h"


static const char* TAG = "BLOCK_SET";

#define BLOCK_SET_TARGET   0
#define BLOCK_SET_VALUE    1


/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

emu_result_t block_set(block_handle_t *block) {
    emu_result_t res= EMU_RESULT_OK();
    if(!emu_check_updated(block, BLOCK_SET_VALUE)){EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");}
    res = mem_set(block->inputs[BLOCK_SET_TARGET], mem_get(block->inputs[BLOCK_SET_VALUE], false));
    if(!res.code){LOG_I(TAG, "SET CODE %s", EMU_ERR_TO_STR(res.code));}
    return res;
}


