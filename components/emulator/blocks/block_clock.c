#include "block_clock.h"
#include "emulator_blocks.h"
#include "emulator_errors.h"
#include "emulator_variables_acces.h"
#include "emulator_loop.h" 
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char* TAG = "BLOCK_CLOCK";

typedef struct {
    double   default_period;
    double   default_width;
    uint64_t start_time_ms; // 
    bool     prev_en;       //
} block_clock_handle_t;

#define CLK_IN_EN      0 //If input is updated and true then block will activate
#define CLK_IN_PERIOD  1
#define CLK_IN_WIDTH   2
#define CLK_OUT_Q      0


extern emu_loop_handle_t loop_handle; 

emu_result_t block_clock(block_handle_t *block) {

    block_clock_handle_t *data = (block_clock_handle_t*)block->custom_data;

    bool en = emu_block_check_and_get_en(block, CLK_IN_EN);
    if (!en) {
        data->prev_en = false;
        emu_variable_t v_out = { .type = DATA_B, .data.b = false };
        emu_result_t res = emu_block_set_output(block, &v_out, CLK_OUT_Q);
        if (unlikely(res.code != EMU_OK)) EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output Set Fail");
        return EMU_RESULT_OK();
    }

    double period = data->default_period;
    if(emu_check_updated(block, CLK_IN_PERIOD)){MEM_GET(&period, block->inputs[CLK_IN_PERIOD]);}

    double width = data->default_width;
    if(emu_check_updated(block, CLK_IN_WIDTH)){MEM_GET(&width, block->inputs[CLK_IN_WIDTH]);}

    if (period < 1.0) period = 1.0; 
    if (width < 0.0) width = 0.0;

    uint64_t now = loop_handle->timer.time; 

    if (!data->prev_en) {
        data->start_time_ms = now;
        data->prev_en = true;
    }

    uint64_t local_time = now - data->start_time_ms;
    
    double phase = fmod((double)local_time, period);

    bool q_state = (phase < width);
    LOG_I(TAG, "phase%lf,width %lf", phase, width);

    emu_variable_t v_out = { .type = DATA_B, .data.b = q_state };
    LOG_I(TAG, "time %lld", local_time);
    LOG_I(TAG, "out %d ", q_state);
    emu_result_t res = emu_block_set_output(block, &v_out, CLK_OUT_Q);
    
    if (unlikely(res.code != EMU_OK)) {EMU_RETURN_CRITICAL(res.code, block->cfg.block_idx, TAG, "Output Set Fail");}
    return EMU_RESULT_OK();
}

/* ============================================================================
    HELPERY (PARSE / VERIFY / FREE)
   ============================================================================ */

emu_result_t block_clock_parse(chr_msg_buffer_t *source, block_handle_t *block) {
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "Block NULL");

    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint16_t target_idx = block->cfg.block_idx;

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (len < 5) continue;

        if (data[0] == 0xBB && data[1] == block->cfg.block_type) { 
            uint16_t pkt_idx;
            memcpy(&pkt_idx, &data[3], 2);

            if (pkt_idx == target_idx) {
                
                if (block->custom_data == NULL) {
                    block->custom_data = calloc(1, sizeof(block_clock_handle_t));
                    if (!block->custom_data) {
                        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, target_idx, TAG, "Alloc failed");
                    }
                }
                block_clock_handle_t *config = (block_clock_handle_t*)block->custom_data;

                if (data[2] == EMU_H_BLOCK_PACKET_CONST) {
                    if (len < (5 + 16)) {
                        EMU_RETURN_CRITICAL(EMU_ERR_INVALID_DATA, target_idx, TAG, "Packet too short");
                    }
                    size_t offset = 5;
                    memcpy(&config->default_period, &data[offset], 8); offset += 8;
                    memcpy(&config->default_width,  &data[offset], 8); offset += 8;

                    LOG_I(TAG, "Parsed Config: Period=%.2f ms, Width=%.2f ms", 
                          config->default_period, config->default_width);
                }
            }
        }
    }
    return EMU_RESULT_OK();
}

emu_result_t block_clock_verify(block_handle_t *block) {
    if (!block) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, 0, TAG, "Block NULL");
    if (!block->custom_data) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, block->cfg.block_idx, TAG, "Custom Data Missing");

    block_clock_handle_t *config = (block_clock_handle_t*)block->custom_data;

    if (config->default_period < 0.001) {
        EMU_RETURN_WARN(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, TAG, "Default Period near zero");
    }
    return EMU_RESULT_OK();
}

void block_clock_free(block_handle_t* block) {
    if (block && block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_I(TAG, "Freed custom data");
    }
}