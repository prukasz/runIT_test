#include "block_latch.h"
#include "emu_logging.h"
#include "emu_variables_acces.h" 
#include "emu_blocks.h"
#include <stdint.h>
#include <string.h>

static const char* TAG = __FILE_NAME__;

#define BLOCK_LATCH_EN       0
#define BLOCK_LATCH_SET      1
#define BLOCK_LATCH_RESET    2

typedef struct{
    uint8_t state : 1;
    uint8_t latch_type : 7;
}block_latch_handle_t;
/*-------------------------------BLOCK IMPLEMENTATION---------------------------------------------- */
#undef OWNER
#define OWNER EMU_OWNER_block_latch

emu_result_t block_latch(block_handle_t block) {
    if(!block_check_in_true(block, BLOCK_LATCH_EN)) {RET_OK_INACTIVE(block->cfg.block_idx);}

    block_latch_handle_t* latch = (block_latch_handle_t*)block->custom_data;
    bool set = block_check_in_true(block, BLOCK_LATCH_SET);
    bool reset = block_check_in_true(block, BLOCK_LATCH_RESET);

    if (set && !reset) {
        latch->state = true;}
    else{
        switch (latch->latch_type) {
            case 0: // SR LATCH
                if (!set && reset) latch->state = false;
                else if (set && reset) latch->state = true;
                break;
            case 1: // RS LATCH
                if (!set && reset) latch->state = false;
                else if (set && reset) latch->state = false;
                break;
            }
        }
    *block->outputs[0]->instance->data.b = latch->state;
    block->outputs[0]->instance->updated = 1;
    return EMU_RESULT_OK();
}

/*-------------------------------BLOCK PARSER------------------------------------------------------- */

#undef OWNER
#define OWNER EMU_OWNER_block_latch_parse
emu_result_t block_latch_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");

    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");

    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;

      if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(block_latch_handle_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "[%d]Null handle ptr", block->cfg.block_idx);
    }

    block_latch_handle_t* latch_config = (block_latch_handle_t*)block->custom_data;
    
    if (packet_id == BLOCK_PKT_CFG) {
        if (payload_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "CONFIG payload too short");

        memcpy(latch_config, &payload[0], sizeof(block_latch_handle_t));

        LOG_I(TAG, "Parsed CONFIG: BlockId=%"PRIu16" Type=%s", block->cfg.block_idx, latch_config->latch_type ? "RS LATCH" : "SR LATCH");
    }

    return EMU_RESULT_OK();
}

/*-------------------------------BLOCK VERIFIER----------------------------------------------------- */
#undef OWNER
#define OWNER EMU_OWNER_block_latch_verify
emu_result_t block_latch_verify(block_handle_t block) {
    if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL %d", block->cfg.block_idx);}
    return EMU_RESULT_OK();
}

/*-------------------------------BLOCK FREE FUNCTION------------------------------------------------ */
//handled by generic 
void block_latch_free(block_handle_t block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "[%d]Cleared latch block data", block->cfg.block_idx);
    }
    return;
}

