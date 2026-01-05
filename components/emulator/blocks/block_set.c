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

    emu_result_t res = EMU_RESULT_OK();
    if(!emu_block_check_inputs_updated(block)){EMU_RETURN_NOTICE(EMU_ERR_BLOCK_INACTIVE, block->cfg.block_idx, TAG, "Block is inactive");}

    emu_variable_t var = mem_get(block->inputs[BLOCK_SET_VALUE], false);
    if (likely(var.error == EMU_OK)) {
        res = mem_set(block->inputs[BLOCK_SET_TARGET], var);
    }
    else{EMU_RETURN_CRITICAL(var.error, block->cfg.block_idx, TAG, "Input acces error %s", EMU_ERR_TO_STR(var.error));}
    LOG_I(TAG,"[%d], set value for %lf", block->cfg.block_idx, emu_var_to_double(var));
    return res;
}


