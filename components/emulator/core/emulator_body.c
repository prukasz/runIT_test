#include "emulator_interface.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"

static const char *TAG = "EMULATOR BODY";

/*********************************************************/
/*      SEMAPHOPRES TO RUN LOOP AND WTD CHECK            */
extern SemaphoreHandle_t emu_global_loop_start_semaphore;
extern SemaphoreHandle_t emu_global_loop_wtd_semaphore;
/*********************************************************/

/**********************************************************************/
/*          LIST OF BLOCKS AND THEIR FUCNTIONS TO RUN                 */
//total count of blocks to be execuded in code
uint16_t emu_block_total_cnt;
//This elements of this list are passed into corelating function
block_handle_t** emu_block_struct_execution_list=NULL; 
/**********************************************************************/
/**     
 * @brief execute list of funinctions and corresponding block structs
 */
static inline emu_err_t emu_execute_code(block_handle_t** block_struct_list, uint16_t total_block_cnt); 


void emu_body_loop_task(void* params){
    while(1){
        if(pdTRUE == xSemaphoreTake(emu_global_loop_start_semaphore, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            emu_execute_code(emu_block_struct_execution_list, emu_block_total_cnt);
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %lld us, watchdog triggered: %d", (end_time - start_time), WTD_TRIGGERED());
            xSemaphoreGive(emu_global_loop_wtd_semaphore);
            taskYIELD();
        }
    }
}


__attribute__((always_inline)) static inline emu_err_t emu_execute_code(block_handle_t **block_struct_list, uint16_t total_block_cnt){
    static const char* _TAG = "EMU_BODY_EXECUTE";
    for (uint16_t i = 1; i < total_block_cnt; i++) {
        block_struct_list[i]->in_set = 0;
    }
    block_struct_list[0]->in_set = 0xFFFF;

    for (uint16_t i = 0; i < total_block_cnt; i++) {
        LOG_I(_TAG, "Now will execute block %d", i);
        if(CHECK_ALL_N(block_struct_list[i]->in_set, block_struct_list[i]->in_cnt)){
            emu_err_t err = (block_struct_list[i])->block_function(block_struct_list[i]);
             if (err != EMU_OK) {
                ESP_LOGE(TAG, "Block %d failed during execution, error: %s", i, EMU_ERR_TO_STR(err));
                return err;
            }
        }else{
            ESP_LOGW(TAG, "block %d skipped, some inputs not updated", i);
        }
    }
    return EMU_OK;
}

emu_err_t emu_body_create_execution_table(uint16_t total_blocks_cnt) {
    if (total_blocks_cnt == 0){
        ESP_LOGE(TAG, "Zero blocks to create provided");
        return EMU_ERR_INVALID_ARG;
    }

    emu_block_total_cnt = total_blocks_cnt;
    // Allocate array of pointers to block_handle_t
    emu_block_struct_execution_list = (block_handle_t**)calloc(emu_block_total_cnt, sizeof(block_handle_t*));
    if (!emu_block_struct_execution_list) {
        ESP_LOGE(TAG, "Failed to allocate space for block structs");
        return EMU_ERR_NO_MEM;
    }

    LOG_I(TAG, "Successfully allocated space for %d block structs", total_blocks_cnt);
    return EMU_OK;
}
