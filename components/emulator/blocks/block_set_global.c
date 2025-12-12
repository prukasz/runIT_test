#include "block_set_global.h"
#include "utils_block_in_q_access.h"

static const char* TAG = "B_SET_GLOBAL";
emu_err_t block_set_global(void* block_data){
    block_handle_t *block = (block_handle_t*)block_data;
    global_acces_t *root = block->extras;
    double to_set = utils_get_in_val_safe(0, block, &to_set);
    uint8_t idx[3]= {root->target_custom_indices[0], root->target_custom_indices[1], root->target_custom_indices[2]};

    for(uint8_t i=1; i < block->in_cnt; i++){
        uint8_t set_idx = IN_GET_UI8(block, 1);
        if(set_idx > 0xFE){
            idx[0]=set_idx;
        }else{
            ESP_LOGE(TAG, "Index %d out of range", i);
        }
    }
    mem_set_safe(root->target_type, root->target_idx, idx, to_set);  
    block_pass_results(block);
    return EMU_OK;
}