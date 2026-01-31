#include "block_counter.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include <string.h>

static const char* TAG = __FILE_NAME__;


typedef enum{
    CFG_ON_RISING,
    CFG_WHEN_ACTIVE,
}counter_cfg_t;

typedef struct{
    float current_val;
    float step;
    float max;
    float min;
    float start;
    counter_cfg_t cfg;
    bool prev_ctu; 
    bool prev_ctd;
}counter_handle_t;

// Input indices
#define IN_0_CTU        0
#define IN_1_CTD        1
#define IN_2_RESET      2
#define IN_3_STEP       3   
#define IN_4_LIMIT_MAX  4
#define IN_5_LIMIT_MIN  5

// Output indices
#define OUT_0_ENO       0
#define OUT_1_VAL       1


#undef OWNER
#define OWNER EMU_OWNER_block_counter
emu_result_t block_counter(block_handle_t block) {

    counter_handle_t* data = (counter_handle_t*)block->custom_data;
    emu_result_t res;
    (void)res; // May be used by macros

    if (block_in_updated(block, IN_3_STEP)) {MEM_GET(&data->step, block->inputs[IN_3_STEP]);}


    if (block_in_updated(block, IN_4_LIMIT_MAX)) {MEM_GET(&data->max, block->inputs[IN_4_LIMIT_MAX]);}
    
    if (block_in_updated(block, IN_5_LIMIT_MIN)) {MEM_GET(&data->min, block->inputs[IN_5_LIMIT_MIN]);}


    // RESET (Priority 1)
    if (block_check_EN(block, IN_2_RESET)) {
            data->current_val = data->start;
            data->prev_ctu = false; // Clear edge detection state
            data->prev_ctd = false;
            goto finish; // Exit immediately after reset
    }

    // CTU
    
    if (block_check_EN(block, IN_0_CTU)) {
        
        if (data->cfg == CFG_ON_RISING) {
            if (!data->prev_ctu){
                data->current_val += data->step;
                data->prev_ctu = true;
                if (data->current_val > data->max) data->current_val = data->max;
                goto finish; // CTU handled, skip CTD
            }  
        }else{
            data->current_val += data->step;
            if (data->current_val > data->max) data->current_val = data->max;
            data->prev_ctu = true;
            goto finish;
        }
        }else{
            data->prev_ctu = false;
        }

        if (block_check_EN(block, IN_1_CTD)) {
        if (data->cfg == CFG_ON_RISING) {
            if (!data->prev_ctd){
                data->current_val -= data->step;
                data->prev_ctd = true;
                if (data->current_val < data->min) data->current_val = data->min;
                goto finish; // CTU handled, skip CTD
            }    
        }else{
            data->current_val -= data->step;
            if (data->current_val < data->min) data->current_val = data->min;
            data->prev_ctd = true;
            goto finish;
        }
        }else{
            data->prev_ctu = false;
        }
LOG_I(TAG, "FU");
return EMU_RESULT_OK();
finish: 
    //LOG_I(TAG, "CURRENT, START, STEP, MAX, MIN: %lf, %lf, %lf, %lf, %lf", data->current_val, start, data->step, data->max, data->min);
    // OUT_0: ENO (True/Active)
    LOG_I(TAG, "Setting outputs in counter block");
    mem_var_t v_eno = { .type = MEM_B, .data.val.b = true };
    res = block_set_output(block, v_eno, OUT_0_ENO);
    if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, 0, "Set ENO Error");

    // OUT_1: VAL (Current Value)
    mem_var_t v_val = { .type = MEM_F, .data.val.f = data->current_val };
    res = block_set_output(block, v_val, OUT_1_VAL);
    if (unlikely(res.code != EMU_OK)) RET_ED(res.code, block->cfg.block_idx, 0, "Set VAL Error");

    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_block_counter_parse
emu_result_t block_counter_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(counter_handle_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "[%d]Null handle ptr", block->cfg.block_idx);
    }
    
    counter_handle_t *handle = (counter_handle_t*)block->custom_data;
    
    // PKT_CFG (0x01): [mode:u8][start:f32][step:f32][max:f32][min:f32]
    if (packet_id == BLOCK_PKT_CFG) {
        if (payload_len < 17) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "Config payload too short");
        
        size_t offset = 0;
        handle->cfg = (counter_cfg_t)payload[offset++];
        
        memcpy(&handle->start, &payload[offset], 4); offset += 4;
        memcpy(&handle->step,  &payload[offset], 4); offset += 4;
        memcpy(&handle->max,   &payload[offset], 4); offset += 4;
        memcpy(&handle->min,   &payload[offset], 4); offset += 4;
        
        handle->current_val = handle->start;
        handle->prev_ctu = false;
        handle->prev_ctd = false;
        
        LOG_I(TAG, "Counter Config: mode=%d, start=%.2f, step=%.2f, max=%.2f, min=%.2f",
              handle->cfg, handle->start, handle->step, handle->max, handle->min);
    }
    
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_block_counter_verify
emu_result_t block_counter_verify(block_handle_t block) {
    if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL %d", block->cfg.block_idx);}
    return EMU_RESULT_OK();
}

void block_counter_free(block_handle_t block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "[%d]Cleared counter block data", block->cfg.block_idx);
    }
    return;
}


