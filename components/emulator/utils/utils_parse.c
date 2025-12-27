#include "utils_parse.h"
#include "emulator_blocks.h"
#include "blocks_all_list.h"

bool parse_check_header(uint8_t *data, emu_header_t header)
{   
    return (bool)(GET_HEADER(data) == (emu_header_t)header);
}

emu_err_t utils_parse_fuction_assign_to_block(block_handle_t *block){
    static const char* TAG = "FUNCTION ASSIGN";
    switch (block->block_type)
    {
    case BLOCK_MATH:
        block->block_function = block_math;
        ESP_LOGI(TAG, "Assigned [block_math] to block idx %d", block->block_idx);
        break;
    case BLOCK_SET_GLOBAL:
        block->block_function = block_set_global;
        ESP_LOGI(TAG, "Assigned [block_set_global] to block idx %d", block->block_idx);
        break;
    case BLOCK_CMP:
        block->block_function = block_logic;
        ESP_LOGI(TAG, "Assigned [block_logic] to block idx %d", block->block_idx);
        break;
    case BLOCK_FOR:
        block->block_function = block_for;
        ESP_LOGI(TAG, "Assigned [block_for] to block idx %d", block->block_idx);
        break;
    default:
        ESP_LOGE(TAG, "Unknown block type %d for block id %d", block->block_type, block->block_idx);
        break;
    }
    return EMU_OK;
}

