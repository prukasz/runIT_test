#include "block_math.h"

int cnt;
emu_err_t block_math(void* block_data){
    block_handle_t *block = (block_handle_t*)block_data;
    _global_acces_t *test = block->global_reference[0];
    double val ;
    uint8_t table[3]= {0xFF,0xFF,0xFF};
    //ESP_LOGI("GLOBALACCES","resuld by hand %d",MEM_GET_U8(0,table));

    uint8_t table2[3]= {0x01,0xFF,0xFF};
    //ESP_LOGI("GLOBALACCES","res uld by hand2 %d",MEM_GET_U16(0,table2));
    if(block->block_id == 0){
        double result = 0.0;
        utils_global_var_acces_recursive(test, &result);
        utils_global_var_acces_recursive(test, &result);
        utils_global_var_acces_recursive(test, &result);
        utils_global_var_acces_recursive(test, &result);
        utils_global_var_acces_recursive(test, &result);
        //ESP_LOGI("GLOBALACCES","resuld %lf", result);
    }
    block_pass_results(block);
    return EMU_OK;      
}   