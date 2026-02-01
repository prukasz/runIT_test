#include "block_clock.h"
#include "emu_blocks.h"
#include "emu_logging.h"
#include "emu_variables_acces.h"
#include "emu_loop.h" 
#include "esp_log.h"
#include <string.h>

static const char* TAG = __FILE_NAME__;

typedef struct {
    uint32_t default_period;
    uint32_t default_width;
    uint64_t start_time_ms; // 
    bool     prev_en;       //
} block_clock_handle_t;

#define CLK_IN_EN      0 //If input is updated and true then block will activate
#define CLK_IN_PERIOD  1
#define CLK_IN_WIDTH   2
#define CLK_OUT_Q      0


#undef OWNER
#define OWNER EMU_OWNER_block_clock
emu_result_t block_clock(block_handle_t block) {

    block_clock_handle_t *data = (block_clock_handle_t*)block->custom_data;

    bool en = block_check_in_true(block, CLK_IN_EN);
    if (!en) {
        data->prev_en = false;
        mem_var_t v_out = { .type = MEM_B, .data.val.b = false };
        emu_result_t res = block_set_output(block, v_out, CLK_OUT_Q);
        if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, 0, "Output Set Fail");
        return EMU_RESULT_OK();
    }

    uint32_t period = data->default_period;
    if(block_in_updated(block, CLK_IN_PERIOD)){MEM_GET(&period, block->inputs[CLK_IN_PERIOD]);}

    uint32_t width = data->default_width;
    if(block_in_updated(block, CLK_IN_WIDTH)){MEM_GET(&width, block->inputs[CLK_IN_WIDTH]);}

    if (period < 1) period = 1;

    uint64_t now = emu_loop_get_time();

    if (!data->prev_en) {
        data->start_time_ms = now;
        data->prev_en = true;
    }

    uint64_t local_time = now - data->start_time_ms;
    
    uint32_t phase = (uint32_t)(local_time % period);

    bool q_state = (phase < width);
    if(q_state == true){
        REP_MSG(EMU_LOG_clock_out_active, block->cfg.block_idx, "Clock output ACTIVE (phase: %lu ms < width: %lu ms)", phase, width);
    }else{
        REP_MSG(EMU_LOG_clock_out_inactive, block->cfg.block_idx, "Clock output INACTIVE (phase: %lu ms >= width: %lu ms)", phase, width);
    }
    mem_var_t v_out = { .type = MEM_B, .data.val.b = q_state, .by_reference = 0 };
    emu_result_t res = block_set_output(block, v_out, CLK_OUT_Q);
    
    if (unlikely(res.code != EMU_OK)) {RET_ED(res.code, block->cfg.block_idx, 0, "Output Set Fail");}
    return EMU_RESULT_OK();
}

/* ============================================================================
    HELPERY (PARSE / VERIFY / FREE)
   ============================================================================ */

#undef OWNER
#define OWNER EMU_OWNER_block_clock_parse
emu_result_t block_clock_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "Block NULL");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(block_clock_handle_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "Alloc failed");
    }
    
    block_clock_handle_t *config = (block_clock_handle_t*)block->custom_data;
    
    // PKT_CFG (0x01): [period:u32][width:u32]
    if (packet_id == BLOCK_PKT_CFG) {
        if (payload_len < 8) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "Config payload too short");
        
        memcpy(&config->default_period, &payload[0], 4);
        memcpy(&config->default_width,  &payload[4], 4);
        
        LOG_I(TAG, "Parsed Config: Period=%lu ms, Width=%lu ms", 
              (uint32_t)config->default_period, (uint32_t)config->default_width);
    }
    
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_block_clock_verify
emu_result_t block_clock_verify(block_handle_t block) {
    if (!block) RET_E(EMU_ERR_NULL_PTR, "Block NULL");
    if (!block->custom_data) RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data Missing");

    block_clock_handle_t *config = (block_clock_handle_t*)block->custom_data;

    if (config->default_period == 0) {
        RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Default Period is zero");
    }
    return EMU_RESULT_OK();
}

void block_clock_free(block_handle_t block) {
    if (block && block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_I(TAG, "Freed custom data");
    }
}
