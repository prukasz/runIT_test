#include "emulator_interface.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "block_types.h"
#include "emulator_body.h"
#include "blocks_functions_list.h"

static const char *TAG = "EMULATOR BODY";

static emu_code_handle_t global_code_ctx = NULL;

/** 
 * * @brief execute list of funinctions and corresponding block structs
 */
uint64_t emu_loop_iterator;
static emu_result_t emu_execute_code(block_handle_t **block_struct_list, uint16_t total_block_cnt){

    emu_result_t res = {.code = EMU_OK};
    static const char* _TAG2 = "EMU_BODY_EXECUTE";
    
    if (!block_struct_list) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_execute_code, 0, 0, _TAG2, "Block struct list is NULL");
    }
    
    for (emu_loop_iterator = 0; emu_loop_iterator < total_block_cnt; emu_loop_iterator++) {
        EMU_REPORT(EMU_LOG_executing_block, EMU_OWNER_emu_execute_code, emu_loop_iterator, TAG, "Executing block %d", emu_loop_iterator);
        emu_block_reset_outputs_status(block_struct_list[emu_loop_iterator]);
        
        if(!emu_loop_wtd_status()){
            res = blocks_main_functions_table[block_struct_list[emu_loop_iterator]->cfg.block_type](block_struct_list[emu_loop_iterator]);
            
            if (res.code != EMU_OK && res.code != EMU_ERR_BLOCK_INACTIVE){
                EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_execute_code, emu_loop_iterator, ++res.depth, _TAG2, 
                                    "Block %d (error owner idx: %d) failed during execution, error: %s", 
                                    emu_loop_iterator, res.owner_idx, EMU_ERR_TO_STR(res.code));
            }
        } else if(emu_loop_wtd_status()){
            EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_WTD_TRIGGERED, EMU_OWNER_emu_execute_code, emu_loop_iterator, 0, _TAG2, 
                                "While executing loop %lld, after block %d, watchdog triggered, total running time %lld ms, wtd is set to %lld ms",
                                emu_loop_get_iteration(), emu_loop_iterator,
                                emu_loop_get_time(), (uint64_t)(emu_loop_get_wtd_max_skipped() * emu_loop_get_period()) / 1000);
        }
    }
    return res;
}


void emu_body_loop_task(void* params){

    while(1){
        if(emu_loop_wait_for_cycle_start(portMAX_DELAY)==true){ 
            int64_t start_time = esp_timer_get_time();

            emu_execute_code(emu_block_struct_execution_list, emu_block_total_cnt);
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Loop completed in %lld us", (end_time - start_time));
            //debug_blocks_value_dump(emu_block_struct_execution_list, emu_block_total_cnt, 100);
            emu_loop_notify_cycle_end();
            taskYIELD();
        }
    }
}



emu_code_handle_t emu_get_current_code_ctx(){
    return global_code_ctx;
}




