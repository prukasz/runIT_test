#include "utils_parse.h"
#include "emulator_blocks.h"
#include "block_math.h"

bool parse_check_header(uint8_t *data, emu_header_t header)
{   
    return (bool)(GET_HEADER(data) == (emu_header_t)header);
}

emu_err_t utils_parse_fuction_assign_to_block(block_handle_t *block){
    static const char* TAG = "FUNCTION ASSIGN";
    switch (block->block_type)
    {
    case BLOCK_COMPUTE:
        block->block_function = block_math;
        ESP_LOGI(TAG, "Assigned [block_math] to block idx %d", block->block_idx);
        break;
    case BLOCK_INJECT:
        break;
    default:
        ESP_LOGE(TAG, "Unknown block type %d for block id %d", block->block_type, block->block_idx);
        break;
    }
    return EMU_OK;
}