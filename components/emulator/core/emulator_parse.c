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
        ESP_LOGI(TAG, "Parse Manager Reset");
        break;

    // --- VARIABLES (Obsługa wielokrotnej alokacji dla Globalnych i Bloków) ---
    case PARSE_CREATE_VARIABLES:
        res = emu_mem_parse_create_context(source); 
        if (res.code == EMU_OK || res.warning == 1) {
            emu_result_t r_scal = emu_mem_parse_create_scalar_instances(source);
            if (r_scal.code != EMU_OK && r_scal.warning != 1) res = r_scal;
            emu_result_t r_arr = emu_mem_parse_create_array_instances(source);
            if (r_arr.code != EMU_OK && r_arr.warning != 1) res = r_arr;
        }

        // Aktualizacja stanu
        if (res.code == EMU_OK || res.warning == 1) {
            if (!p_state.vars_allocated) {
                p_state.vars_allocated = true;
                ESP_LOGI(TAG, "Variables Allocation Started (First Pass)");
            } else {
                // To jest normalne przy drugim wywołaniu dla drugiego kontekstu
                ESP_LOGD(TAG, "Variables Allocation Update (Subsequent Pass)");
            }
        }
        break;

    case PARSE_FILL_VARIABLES:
        if (p_state.vars_allocated) {
            res = emu_mem_parse_context_data_packets(source, NULL); 
            if (res.code == EMU_OK) p_state.vars_filled = true;
        } else {
            res.code = EMU_ERR_DENY;
            ESP_LOGW(TAG, "Attempt to fill variables before allocation");
        }
        break;

    // --- BLOCKS ---
    case PARSE_CREATE_BLOCKS_LIST:
        if (p_state.vars_filled && !p_state.blocks_list_alloc) {
            res = emu_parse_total_block_cnt(source, &emu_block_struct_execution_list, &emu_block_total_cnt);
            if (res.code == EMU_OK) {
                p_state.blocks_list_alloc = true;
                ESP_LOGI(TAG, "Block List Allocated (%d)", emu_block_total_cnt);
            }
        } else if (!p_state.vars_filled) {
             ESP_LOGE(TAG, "List alloc denied: Vars not filled");
             res.code = EMU_ERR_DENY;
        }
        break;

    case PARSE_CREATE_BLOCKS:
    case PARSE_FILL_BLOCKS: 
        if (p_state.blocks_list_alloc) {
            // Parsowanie bloków (Config + Custom Data)
            res = emu_parse_block(source, emu_block_struct_execution_list, emu_block_total_cnt);
            
            if (res.code == EMU_OK) {
                p_state.blocks_parsed = true;
            }
        } else {
            res.code = EMU_ERR_DENY;
            ESP_LOGE(TAG, "Create blocks denied: List not alloc");
        }
        break;

    // --- CHECK ---
    case PARSE_CHECK_CAN_RUN:
        if (p_state.vars_filled && p_state.blocks_parsed) {
            return res; // OK
        } else {
            res.code = EMU_ERR_DENY;
            return res;
        }
        break;

    default:
        res.code = EMU_ERR_PARSE_INVALID_REQUEST;
        break;
    }

    return res;
}