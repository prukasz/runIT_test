#include "block_math.h"

int cnt;
emu_err_t block_math(void* str){

   block_handle_t *block = (block_handle_t*)str;
    ESP_LOGI("compute_block", "block block_id: %d executing %d, val0 %lf, val1 %lf",block->block_id, cnt++, get_in_val(0, block), get_in_val(1, block));
    
    
    if (block->block_id==0){
        double v;
        _global_val_acces_t  * test = (_global_val_acces_t  *)block->extras;
        ESP_LOGI("return of memget", "%d", emu_global_var_acces_recursive(test, &v));
        printf("Result = %f\n", v);  // oczekiwany wynik np. 7.0
    }
        ESP_LOGI("compute_block", "making copy");
        block_pass_results(block);
    return EMU_OK;      
}