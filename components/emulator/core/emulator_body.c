#include "emulator_interface.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"
#include "emulator_loop.h"
#include "emulator_logging.h"
#include "esp_log.h"
#include "block_types.h"
//#include "blocks_functions_list.h"


static const char *TAG = "EMULATOR BODY";

/*********************************************************/
/* SEMAPHOPRES TO RUN LOOP AND WTD CHECK            */
extern SemaphoreHandle_t emu_global_loop_start_semaphore;
extern SemaphoreHandle_t emu_global_loop_wtd_semaphore;
/*********************************************************/

/**********************************************************************/
/* LIST OF BLOCKS AND THEIR FUCNTIONS TO RUN                 */
//total count of blocks to be execuded in code
uint16_t emu_block_total_cnt;
//This elements of this list are passed into corelating function
block_handle_t** emu_block_struct_execution_list=NULL; 
/**********************************************************************/
/** * @brief execute list of funinctions and corresponding block structs
 */
static inline emu_result_t emu_execute_code(block_handle_t** block_struct_list, uint16_t total_block_cnt, emu_loop_handle_t loop_handle); 


void emu_body_loop_task(void* params){
    emu_loop_handle_t loop_ctx = (emu_loop_handle_t*)params;
    
    while(1){
        if(xSemaphoreTake(loop_ctx->sem_loop_start, portMAX_DELAY) == pdTRUE) {
            int64_t start_time = esp_timer_get_time();

            //emu_execute_code(emu_block_struct_execution_list, emu_block_total_cnt, loop_ctx);
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %lld us", (end_time - start_time));
            //debug_blocks_value_dump(emu_block_struct_execution_list, emu_block_total_cnt, 100);
            xSemaphoreGive(loop_ctx->sem_loop_wtd);
            taskYIELD();
        }
    }
}

uint16_t emu_loop_iterator = 0;
static inline emu_result_t emu_execute_code(block_handle_t **block_struct_list, uint16_t total_block_cnt, emu_loop_handle_t loop_handle){
    emu_result_t res = {.code = EMU_OK};
    static const char* _TAG2 = "EMU_BODY_EXECUTE";
    emu_loop_handle_t loop;
    for (emu_loop_iterator = 0; emu_loop_iterator < total_block_cnt; emu_loop_iterator++) {
        EMU_REPORT(EMU_LOG_executing_block, EMU_OWNER_emu_execute_code, emu_loop_iterator, TAG, "Executing block %d", emu_loop_iterator);
        emu_block_reset_outputs_status(block_struct_list[emu_loop_iterator]);
        
        if(!loop->wtd.wtd_triggered){
            res = blocks_main_functions_table[block_struct_list[emu_loop_iterator]->cfg.block_type](block_struct_list[emu_loop_iterator]);
            
            if (res.code != EMU_OK && res.code != EMU_ERR_BLOCK_INACTIVE){
                EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_execute_code, emu_loop_iterator, ++res.depth, _TAG2, 
                                    "Block %d (error block idx: %d) failed during execution, error: %s", 
                                    emu_loop_iterator, res.block_idx, EMU_ERR_TO_STR(res.code));
            }
        } else if(loop->wtd.wtd_triggered == 1){
            EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_WTD_TRIGGERED, EMU_OWNER_emu_execute_code, emu_loop_iterator, 0, _TAG2, 
                                "While executing loop %lld, after block %d, watchdog triggered, total running time %lld ms, wtd is set to %lld ms",
                                loop->timer.loop_counter, emu_loop_iterator,
                                loop->timer.time, (uint64_t)(loop->wtd.max_skipp*loop->timer.loop_period)/1000);
        }
    }
    return res;
}