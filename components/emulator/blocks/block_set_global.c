#include "block_set_global.h"
#include "utils_block_in_q_access.h"

static const char* TAG = "BLOCK_SET_GLOBAL";
emu_result_t block_set_global(block_handle_t* block_data){
    emu_result_t res = {.code = EMU_OK};
    if (!block_data || !block_data->global_reference || !(block_data->global_reference_cnt >= 1)) {
        res.code = EMU_ERR_NULL_PTR;
        res.restart = true; 
        return res;
    }
    global_acces_t *root = block_data->global_reference[0];
    if(block_data->global_reference_cnt == 2){root = (global_acces_t*)block_data->global_reference[1];}
    uint8_t idx_target[3];
    double to_set = 0.0; 
    utils_get_in_val_auto(block_data, 0,&to_set);
    

    global_acces_t* next_ptrs[3] = {root->next0, root->next1, root->next2};

    for (int i = 0; i < 3; i++) {
        // Priority 1: Block Input (Dynamic override) - skip search if provided
        if (block_data->in_cnt > (uint8_t)(i + 1)) {
            uint8_t val = IN_GET_UI8(block_data, i + 1);
            if (val == 0xFF) goto index_error;
            idx_target[i] = val;
        } 
        // Priority 2: Recursive Reference (Search)
        else if (next_ptrs[i]) {
            double temp;
            res.code = utils_global_var_acces_recursive(next_ptrs[i], &temp);
            if (res.code != EMU_OK) {
                res.block_idx = block_data->block_idx;
                res.abort = true;
                return res;
            }
            if ((uint8_t)temp == 0xFF) goto index_error;
            idx_target[i] = (uint8_t)temp;
        } 
        // Priority 3: Static Index
        else {
            idx_target[i] = root->target_custom_indices[i];
        }
    }
    res.code = mem_set_safe(root->target_type, root->target_idx, idx_target, to_set);
    if (res.code != EMU_OK){
        res.abort = true;
        res.block_idx = block_data->block_idx;
        return res;
    } 
    block_pass_results(block_data);
    return res;

    index_error:
        LOG_E(TAG, "Index 0xFF is reserved for dimensional exclusion and cannot be used as a dynamic input index");
        res.code = EMU_ERR_BLOCK_COMPUTE_IDX;
        res.warning = true;
        return res;
}