#include "emulator_block_utils.h"
#include "emulator_variables.h"
#include "esp_log.h"


double block_get_var_autoselect(uint8_t idx, block_handle_t *block){
    if(idx<block->in_cnt){
        return get_in_val(idx, block);
    }else{
        uint8_t _idx = idx - block->in_cnt;
        
    }
    return 0.0;
}







