#include "emulator_parse.h"
#include "emulator_blocks.h"
#include "emulator_body.h"
#include "emulator_types.h"
#include "emulator_logging.h"
#include "emulator_variables.h" 
#include "esp_log.h"
#include <string.h>

static const char *TAG = "EMU_PARSE";
extern emu_mem_t *s_mem_contexts[];
extern chr_msg_buffer_t *source;

static emu_code_handle_t code;
static bool binded_to_code;

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
            if(res.abort){EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_manager,0, ++res.depth, TAG, "Create context failed");}
        }
        if (res.code == EMU_OK || res.warning == 1) {
            res = emu_mem_parse_create_scalar_instances(source);
            if (res.abort) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_manager, 0, ++res.depth, TAG, "Create scalar instances failed");}
            res = emu_mem_parse_create_array_instances(source);
            if (res.abort) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_manager, 0, ++res.depth, TAG, "Create array instances failed");}

        //Update
        if (res.code == EMU_OK || res.warning == 1) {
            if (!p_state.vars_allocated) {
                p_state.vars_allocated = true;
                EMU_REPORT(EMU_LOG_variables_allocated, EMU_OWNER_emu_parse_manager, 0,TAG, "Variables allocated successfully");
            } else {
                EMU_REPORT(EMU_LOG_variables_allocated, EMU_OWNER_emu_parse_manager, 0,TAG, "Variables re-allocation successful");
            }
        }
        break;

    case PARSE_FILL_VARIABLES:
        if (p_state.vars_allocated) {
            res = emu_mem_parse_context_data_packets(source, NULL); 
            if (res.code == EMU_OK) p_state.vars_filled = true;
        } else {
            EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_parse_manager, 0, 0,TAG, "Fill variables denied: Vars not allocated");
        }
        break;

    // --- BLOCKS ---
    case PARSE_CREATE_BLOCKS_LIST:
    // we bind parser to code (list of blocks)
        if(!binded_to_code){
            code = emu_get_current_code_ctx();
            binded_to_code = true;
        }
        if (p_state.vars_filled && !p_state.blocks_list_alloc) {
            res = emu_parse_blocks_total_cnt(source, code);
            if (res.code == EMU_OK) {
                p_state.blocks_list_alloc = true;
                EMU_REPORT(EMU_LOG_blocks_list_allocated, EMU_OWNER_emu_parse_manager, 0, TAG, "Block List Allocated (%d)", code->total_blocks);
            }
        } else if (!p_state.vars_filled) {
            EMU_RETURN_CRITICAL(EMU_ERR_DENY, EMU_OWNER_emu_parse_manager, 0,0, TAG, "List alloc denied: Vars not filled");
        }
        break;

    case PARSE_CREATE_BLOCKS: 
        if (p_state.blocks_list_alloc) {
            res = emu_parse_block(source, code);
            if (res.code == EMU_OK) {p_state.blocks_parsed = true;}
            else{EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_parse_manager, 0, 0,TAG, "Create blocks denied");}
        } else {
            EMU_RETURN_CRITICAL(EMU_ERR_DENY, EMU_OWNER_emu_parse_manager, 0, 0,TAG, "Create blocks denied: List not alloc");
        }
        break;


    case PARSE_CHECK_CAN_RUN:
        if (p_state.vars_filled && p_state.blocks_parsed) {
            return res;
        } else {EMU_RETURN_WARN(EMU_ERR_DENY, EMU_OWNER_emu_parse_manager, 0, 0, TAG, "Can't run parse incomplete");}
        LOG_I(TAG, "Checking code completeness");
        //res = emu_parse_blocks_verify_all(emu_block_struct_execution_list, emu_block_total_cnt);
        break;
    default:
        EMU_RETURN_WARN(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_parse_manager, 0,0, TAG, "Invalid parse command");

        }
    }
    return res;
}


