#include "utils_global_access.h"
#include "emulator_variables.h"
#include "emulator_errors.h"
#include "stdbool.h"
#include "esp_log.h"
#include "math.h"
const static char* TAG = "Global_access";

emu_err_t utils_global_var_acces_recursive(_global_acces_t *root, double *out){
    if (!out){
        ESP_LOGE(TAG, "No output provided to paste result");
        return EMU_ERR_NULL_POINTER;
    }else if (!root){
        ESP_LOGE(TAG, "No root varaible provided");
        return EMU_ERR_NULL_POINTER;
    }
    
    uint8_t idx_table[MAX_ARR_DIMS];
    _global_acces_t *next_arr[MAX_ARR_DIMS] = {root->next0, root->next1, root->next2};
    for (uint8_t i = 0; i < MAX_ARR_DIMS; i++){

        if (NULL!=next_arr[i]) {
            double tmp;
            EMU_RETURN_ON_ERROR(utils_global_var_acces_recursive(next_arr[i], &tmp));

            if (tmp < 0.0) {idx_table[i] = 0;}
            else if (tmp > 0xFE) {idx_table[i] = 0xFE;}
            else {idx_table[i] = (uint8_t)round(tmp);}

        } else {
            idx_table[i] = root->target_custom_indices[i];
            //ESP_LOGI(TAG, "Using static index %d: %d", i, idx_table[i]);
        }
    }

    bool is_scalar = (idx_table[0] == UINT8_MAX && idx_table[1] == UINT8_MAX &&idx_table[2] == UINT8_MAX);

    // ESP_LOGI(TAG, "Target type=%d idx=%u scalar=%d idx=[%d,%d,%d]",
    //         root->target_type, (unsigned)root->target_idx, is_scalar,
    //         idx_table[0], idx_table[1], idx_table[2]);

    /* Bounds checking */
    if (is_scalar) {
        if (root->target_idx >= mem.single_cnt[root->target_type]) {
            ESP_LOGE(TAG, "Scalar index out of range");
            return EMU_ERR_MEM_INVALID_INDEX;
        }
    } else {
        if (root->target_idx >= mem.arr_cnt[root->target_type]) {
            ESP_LOGE(TAG, "Array index out of range");
            return EMU_ERR_MEM_INVALID_INDEX;
        }
    }
    double val;
    EMU_RETURN_ON_ERROR(mem_get_as_d(root->target_type, root->target_idx, idx_table, &val));
    *out = val;
    return EMU_OK;
}


