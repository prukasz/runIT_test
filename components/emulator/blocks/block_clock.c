#include "block_clock.h"
#include "emu_blocks.h"
#include "emu_logging.h"
#include "emu_variables_acces.h"
#include "emu_loop.h" 
#include <string.h>

static const char* TAG = __FILE_NAME__;

/*----------------CONFIG---------------------------------------------------------------------------*/

/**
 * @brief Config struct for clock, unique for each block
 */
typedef struct {
    uint32_t default_period;
    uint32_t default_width;
    uint64_t start_time_ms; //start time is received from emulator loop 
    bool     prev_en;       
} block_clock_cfg_t;

#define CLK_IN_EN      0 //Enable - clock starts counting
#define CLK_IN_PERIOD  1 //Period of updates
#define CLK_IN_WIDTH   2 //width of "on inpulse"

#define CLK_OUT_Q      0

/*-------------------------------BLOCK IMPLEMENTATION---------------------------------------------- */

#undef OWNER
#define OWNER EMU_OWNER_block_clock
emu_result_t block_clock(block_handle_t block) {

    block_clock_cfg_t *cfg = (block_clock_cfg_t*)block->custom_data;

    bool en = block_check_in_true(block, CLK_IN_EN);
    
    if (!en) {
        cfg->prev_en = false;
        mem_var_t v_out = { .type = MEM_B, .data.val.b = false };
        if(cfg->prev_en == true){
            emu_result_t res = block_set_output(block, v_out, CLK_OUT_Q);
            if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, ++res.depth, "[%"PRIu16"] Q set failed, %s", block->cfg.block_idx, EMU_ERR_TO_STR(res.code));
        }
        RET_OK_INACTIVE(block->cfg.block_idx);
    }
    

    //check is there provided other value by user via inputs
    uint32_t period = cfg->default_period;
    if(block_in_updated(block, CLK_IN_PERIOD)){MEM_GET(&period, block->inputs[CLK_IN_PERIOD]);}

    uint32_t width = cfg->default_width;
    if(block_in_updated(block, CLK_IN_WIDTH)){MEM_GET(&width, block->inputs[CLK_IN_WIDTH]);}

    if (period == 0) { period = 1; } 

    uint64_t now = emu_loop_get_time();


    //detect rising edge and update "now time"
    if (!cfg->prev_en) {
        cfg->start_time_ms = now;
        cfg->prev_en = true;
    }

    uint64_t local_time = now - cfg->start_time_ms;
    
    uint32_t phase = (uint32_t)(local_time % period);

    bool q_state = (phase < width);
    if(q_state == true){
        REP_MSG(EMU_LOG_clock_out_active, block->cfg.block_idx, "[%"PRIu16"] Q ACTIVE (phase: %lu ms < width: %lu ms)", block->cfg.block_idx, phase, width);
    }else{
        REP_MSG(EMU_LOG_clock_out_inactive, block->cfg.block_idx, "[%"PRIu16"] Q INACTIVE (phase: %lu ms >= width: %lu ms)",  block->cfg.block_idx,  phase, width);
    }
    mem_var_t v_out = { .type = MEM_B, .data.val.b = q_state};
    emu_result_t res = block_set_output(block, v_out, CLK_OUT_Q);
    
    if (unlikely(res.code != EMU_OK)) {RET_ED(res.code, block->cfg.block_idx, ++res.depth, "[%"PRIu16"] Q set failed, %s", block->cfg.block_idx, EMU_ERR_TO_STR(res.code));}

    return EMU_RESULT_OK();
}



/*-------------------------------BLOCK PARSER------------------------------------------------------- */
#define PACKET_SIZE 9

#undef OWNER
#define OWNER EMU_OWNER_block_clock_parse
emu_result_t block_clock_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {

    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "Block NULL");
    
    // Packet: [packet_id:u8][cfg...]
    if (packet_len < PACKET_SIZE) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet incomplete");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    
    // Allocate custom cfg if not exists
    block->custom_data = calloc(1, sizeof(block_clock_cfg_t));
    if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "Alloc failed");
    
    block_clock_cfg_t *config = (block_clock_cfg_t*)block->custom_data;
    
    // PKT_CFG (0x01): [period:u32][width:u32]
    if (packet_id == BLOCK_PKT_CFG) {
     
        memcpy(&config->default_period, &payload[0], sizeof(uint32_t));
        memcpy(&config->default_width,  &payload[4], sizeof(uint32_t));
        
        LOG_I(TAG, "[%"PRIu16"] Configured: Default Period=%"PRIu32" ms, Default Width=%"PRIu32" u ms",
              block->cfg.block_idx, config->default_period, config->default_width);
    }
    
    return EMU_RESULT_OK();
}

/*-------------------------------BLOCK VERIFIER---------------------------------------------- */

#undef OWNER
#define OWNER EMU_OWNER_block_clock_verify
emu_result_t block_clock_verify(block_handle_t block) {

    if (!block->custom_data) RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "[%"PRIu16"] Config missing", block->cfg.block_idx);

    block_clock_cfg_t *config = (block_clock_cfg_t*)block->custom_data;

    if (config->default_period == 0) {RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Default Period is zero");}

    RET_OKD(block->cfg.block_idx, "[%"PRIu16"] verified", block->cfg.block_idx);
}


/*-------------------------------BLOCK FREE FUNCTION---------------------------------------------- */


void block_clock_free(block_handle_t block) {
    if (block && block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
    }
}
