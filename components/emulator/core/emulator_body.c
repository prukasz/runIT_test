#include "emulator_interface.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "block_types.h"
#include "emulator_body.h"
#include "blocks_functions_list.h"

static const char *TAG = __FILE_NAME__;

static emu_code_handle_t global_code_ctx;

/** 
 * * @brief execute list of funinctions and corresponding block structs
 */
uint64_t emu_loop_iterator;
static emu_result_t emu_execute_code(emu_code_handle_t code){

    emu_result_t res = {.code = EMU_OK};
 
    //don't execute if code is null
    if (!code) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_execute_code, 0, 0, TAG, "Block struct list is NULL");}
    
    //execute all blocks in list
    for (emu_loop_iterator = 0; emu_loop_iterator < code->total_blocks; emu_loop_iterator++) {
        EMU_REPORT(EMU_LOG_executing_block, EMU_OWNER_emu_execute_code, emu_loop_iterator, TAG, "Executing block %lld", emu_loop_iterator);

        //we need to reset outputs updated status before execution of block to ensure proper tracking of updates
        emu_block_reset_outputs_status(code->blocks_list[emu_loop_iterator]);

        //execute only if watchdog not triggered
        if(!emu_loop_wtd_status()){
            res = blocks_main_functions_table[code->blocks_list[emu_loop_iterator]->cfg.block_type](code->blocks_list[emu_loop_iterator]);
            
            //check for errors return only if abort flag is set
            if (res.abort){
                EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_execute_code, emu_loop_iterator, ++res.depth, TAG, 
                                    "Block %lld (error owner idx: %d) failed during execution, error: %s", 
                                    emu_loop_iterator, res.owner_idx, EMU_ERR_TO_STR(res.code));
            }

        //If watchdog triggered during execution of block, abort further execution
        } else if(emu_loop_wtd_status()){
            EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_WTD_TRIGGERED, EMU_OWNER_emu_execute_code, emu_loop_iterator, 0, TAG, 
                                "While executing loop %lld, after block %lld, watchdog triggered, total running time %lld ms, wtd is set to %lld ms",
                                emu_loop_get_iteration(), emu_loop_iterator,
                                emu_loop_get_time(), (uint64_t)(emu_loop_get_wtd_max_skipped() * emu_loop_get_period()) / 1000);
        }
    }
    return res;
}

/**
 * @brief Main loop task for emulator execution
 */
void emu_body_loop_task(void* params){
    while(1){
        if(emu_loop_wait_for_cycle_start(portMAX_DELAY)==true){ 
            int64_t start_time = esp_timer_get_time();

            emu_execute_code(global_code_ctx);

            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Loop completed in %lld us", (end_time - start_time));
            // Request logger to dump accumulated logs/reports and wait until it's done
            if (logger_task_handle) {
                xTaskNotifyGive(logger_task_handle);
                if (logger_done_sem) {
                    xSemaphoreTake(logger_done_sem, portMAX_DELAY);
                }
            } else if (logger_request_sem) {
                xSemaphoreGive(logger_request_sem);
                if (logger_done_sem) {
                    xSemaphoreTake(logger_done_sem, portMAX_DELAY);
                }
            }
            //debug_blocks_value_dump(emu_block_struct_execution_list, emu_block_total_cnt, 100);
            emu_loop_notify_cycle_end();
            taskYIELD();
        }
    }
}


/**
 * @brief Get current code context
 * @return emu_code_handle_t Current code context
 * @note This function uses lazy initialization to create the global code context if it doesn't exist
 */
emu_code_handle_t emu_get_current_code_ctx(){
    //lazy init
    if (!global_code_ctx){
        global_code_ctx = (emu_code_handle_t)calloc(1, sizeof(code_ctx_s));
    }
    return global_code_ctx;
}
/**
 * @brief Reset and free the global code context
 */
void emu_reset_code_ctx(){
    if (global_code_ctx){
        emu_blocks_free_all(global_code_ctx);
        free(global_code_ctx);
        global_code_ctx = NULL;
    }
}



