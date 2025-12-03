#include "utils_block_in_q_access.h"
#include "emulator_variables.h"
#include "esp_log.h"


/**
 * @brief Whien global variables attatched to block then this will automatically handle access between inputs and glabal variables
 */
double block_get_var_autoselect(uint8_t idx, block_handle_t *block){
    if(idx<block->in_cnt){
        return get_in_val(idx, block);
    }else{
        uint8_t _idx = idx - block->in_cnt;
        
    }
    return 0.0;
}







