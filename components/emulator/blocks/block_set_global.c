#include "block_set_global.h"
#include "utils_block_in_q_access.h"

static const char* TAG = "BLOCK_SET_GLOBAL";
emu_err_t block_set_global(block_handle_t* block_data){
    if (!block_data || !block_data->global_reference || !block_data->global_reference[0]) {
        return EMU_ERR_NULL_PTR;
    }

    global_acces_t *root = (global_acces_t*)block_data->global_reference[0];
    uint8_t idx_target[3];
    double to_set = IN_GET_D(block_data, 0);

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
            if (utils_global_var_acces_recursive(next_ptrs[i], &temp) != EMU_OK) {
                return EMU_ERR_INVALID_DATA;
            }
            if ((uint8_t)temp == 0xFF) goto index_error;
            idx_target[i] = (uint8_t)temp;
        } 
        // Priority 3: Static Index
        else {
            idx_target[i] = root->target_custom_indices[i];
        }
    }

    mem_set_safe(root->target_type, root->target_idx, idx_target, to_set);  
    return EMU_OK;

index_error:
    LOG_E(TAG, "Index 0xFF is reserved for dimensional exclusion and cannot be used as a dynamic input index");
    return EMU_ERR_BLOCK_COMPUTE_INVALID_INDEX;
}