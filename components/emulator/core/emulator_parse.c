#include "emulator_parse.h"
#include "emulator_blocks.h"
#include "emulator_types.h"
#include "emulator_errors.h"
#include "emulator_variables.h" 
#include "esp_log.h"
#include <string.h>

static const char *TAG = "EMU_PARSE";

extern chr_msg_buffer_t *source;
extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;

// Flagi stanu
static struct {
    bool vars_allocated;
    bool vars_filled;       
    bool blocks_list_alloc;
    bool blocks_parsed;     
} p_state = {0};

emu_result_t emu_parse_manager(parse_cmd_t cmd){
    emu_result_t res = {.code = EMU_OK};

    switch (cmd)
    {
    case PARSE_RESTART:
        memset(&p_state, 0, sizeof(p_state));
        ESP_LOGD(TAG, "Parse Manager Reset");
        break;

    case PARSE_CREATE_VARIABLES:
        if(!s_mem_contexts[0]||!s_mem_contexts[1]){
            res = emu_mem_parse_create_context(source); 
            if(res.code != EMU_OK){EMU_RETURN_CRITICAL(res.code, 0xFFFF,TAG, "Can't create context error: %s", EMU_ERR_TO_STR(res.code));}
        }
        if (res.code == EMU_OK || res.warning == 1) {
            emu_result_t r_scal = emu_mem_parse_create_scalar_instances(source);
            if (r_scal.code != EMU_OK && r_scal.warning != 1) res = r_scal;
            emu_result_t r_arr = emu_mem_parse_create_array_instances(source);
            if (r_arr.code != EMU_OK && r_arr.warning != 1) res = r_arr;
        }

        //Update
        if (res.code == EMU_OK || res.warning == 1) {
            if (!p_state.vars_allocated) {
                p_state.vars_allocated = true;
                LOG_D(TAG, "Variables Allocation Started (First Pass)");
            } else {
                LOG_D(TAG, "Variables Allocation Update (Subsequent Pass)");
            }
        }
        break;

    case PARSE_FILL_VARIABLES:
        if (p_state.vars_allocated) {
            res = emu_mem_parse_context_data_packets(source, NULL); 
            if (res.code == EMU_OK) p_state.vars_filled = true;
        } else {
            EMU_RETURN_WARN(EMU_ERR_DENY, 0xFFFF, TAG, "Attempt to fill variables before allocation");
        }
        break;

    // --- BLOCKS ---
    case PARSE_CREATE_BLOCKS_LIST:
        if (p_state.vars_filled && !p_state.blocks_list_alloc) {
            res = emu_parse_blocks_total_cnt(source, &emu_block_struct_execution_list, &emu_block_total_cnt);
            if (res.code == EMU_OK) {
                p_state.blocks_list_alloc = true;
                LOG_D(TAG, "Block List Allocated (%d)", emu_block_total_cnt);
            }
        } else if (!p_state.vars_filled) {
            EMU_RETURN_CRITICAL(EMU_ERR_DENY, 0xFFFF, TAG, "List alloc denied: Vars not filled");
        }
        break;

    case PARSE_CREATE_BLOCKS: 
        if (p_state.blocks_list_alloc) {
            res = emu_parse_block(source, emu_block_struct_execution_list, emu_block_total_cnt);
            if (res.code == EMU_OK) {p_state.blocks_parsed = true;}
            else{EMU_RETURN_CRITICAL(res.code, 0xFFFF, TAG, "Create blocks denied: %s", EMU_ERR_TO_STR(res.code));}
        } else {
            EMU_RETURN_CRITICAL(EMU_ERR_DENY, 0xFFFF, TAG, "Create blocks denied: List not alloc");
        }
        break;


    case PARSE_CHECK_CAN_RUN:
        if (p_state.vars_filled && p_state.blocks_parsed) {
            return res;
        } else {EMU_RETURN_WARN(EMU_ERR_DENY, 0xFFFF, TAG, "Can't run parse incomplete");}
        LOG_I(TAG, "Checking code completeness");
        res = emu_parse_blocks_verify_all(emu_block_struct_execution_list, emu_block_total_cnt);
        break;
    default:
        EMU_RETURN_WARN(EMU_ERR_INVALID_ARG, 0xFFFF, TAG, "Invalid parse command");
    }

    return res;
}