#include "block_timer.h"
#include "emulator_loop.h" // Contains emu_loop_ctx_t definition
#include "utils_block_in_q_access.h"
#include "utils_parse.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "BLOCK_TIMER";

extern emu_loop_handle_t loop_handle; 

emu_result_t block_timer(block_handle_t *block) {
    uint32_t delta_ms = (uint32_t)(loop_handle->timer.loop_period / 1000);
    if (delta_ms == 0) delta_ms = 1;

    return EMU_RESULT_OK();
}

#define MSG_TIMER_CONFIG 0x01

/* Helper to parse config packet */
static emu_err_t _emu_parse_timer_config(uint8_t *data, uint16_t len, block_timer_t* handle) {
    // Format assumption: [Header:5] + [Type:1] + [DefaultPT:4]
    if (len < 10) {
        ESP_LOGE(TAG, "Parser: Data too short (%d < 10)", len);
        return EMU_ERR_INVALID_DATA;
    }

    size_t offset = 5;
    handle->type = (block_timer_type_t)data[offset++];
    
    // Read 32-bit int for Preset Time
    // Assuming Little Endian in protocol
    memcpy(&handle->default_pt, &data[offset], sizeof(uint32_t));
    
    ESP_LOGI(TAG, "Parser: Config Loaded -> Type=%d (1=TON,2=TOF,3=TP), DefaultPT=%lu ms", handle->type, handle->default_pt);
    return EMU_OK;
}

emu_result_t emu_parse_block_timer(chr_msg_buffer_t *source, uint16_t block_idx) {
    emu_result_t res = EMU_RESULT_OK();
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    // External reference to the main block list
    extern block_handle_t **emu_block_struct_execution_list;

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        // Check Header: [BB] [BlockType] [SubType] [ID_L] [ID_H]
        // Assuming BLOCK_TIMER ID is defined in your Enums
        // block_idx check
        uint16_t msg_id = READ_U16(data, 3);
        
        if (data[0] == 0xBB && data[1] == BLOCK_TIMER && msg_id == block_idx) {
            
            block_handle_t *block = emu_block_struct_execution_list[block_idx];
            
            // Allocation
            if (block->extras == NULL) {
                ESP_LOGI(TAG, "[%d] Allocating memory for Timer extras", block_idx);
                block->extras = calloc(1, sizeof(block_timer_t));
                if (!block->extras) {
                    ESP_LOGE(TAG, "[%d] Memory allocation failed", block_idx);
                    return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, block_idx);
                }
            }
            
            block_timer_t* extras = (block_timer_t*)block->extras;

            // Packet Handler
            if (data[2] == MSG_TIMER_CONFIG) {
                if (_emu_parse_timer_config(data, len, extras) != EMU_OK) {
                    return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_DATA, block_idx);
                }
            }
        }
    }
    return res;
}