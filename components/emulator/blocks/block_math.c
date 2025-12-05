#include "block_math.h"


int cnt;
emu_err_t block_math(void* block_data){
    block_handle_t *block = (block_handle_t*)block_data;
    utils_set_q_val_safe(block, 0, 100+utils_get_in_val_autoselect(0, block)*21.370);
    ESP_LOGI("compute_block", "block block_id: %d executing %d, val0 %lf, val1 %lf",block->block_id, cnt++, utils_get_in_val_autoselect(0, block), utils_get_in_val_autoselect(1, block));
    block_pass_results(block);
    return EMU_OK;      
}