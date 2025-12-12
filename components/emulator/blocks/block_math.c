#include "block_math.h"

int cnt;
emu_err_t block_math(block_handle_t* block_data){
    double val ;
    // uint8_t table[3]= {0xFF,0xFF,0xFF};
    // //ESP_LOGI("GLOBALACCES","resuld by hand %d",MEM_GET_U8(0,table));

    // uint8_t table2[3]= {0x01,0xFF,0xFF};
    // //ESP_LOGI("GLOBALACCES","res uld by hand2 %d",MEM_GET_U16(0,table2));
    double result = 0.0;
    uint8_t table[3]= {255,255,255};
    ESP_LOGI("normall access","result1 %ld",MEM_GET_U32(0, table));
    utils_global_var_acces_recursive(block_data->global_reference[0], &result);
    ESP_LOGI("GLOBALACCES","result1 %lf", result);
    utils_global_var_acces_recursive(block_data->global_reference[1], &result);
    ESP_LOGI("GLOBALACCES","result2 %lf", result);
        // utils_global_var_acces_recursive(test, &result);
        // utils_global_var_acces_recursive(test, &result);
        // utils_global_var_acces_recursive(test, &result);
       
    block_pass_results(block_data);
    return EMU_OK;      
}   