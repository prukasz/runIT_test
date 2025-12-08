#include "emulator_interface.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"

static const char *TAG = "BODY_TASK";

/*********************************************************/
/*      SEMAPHOPRES TO RUN LOOP AND WTD CHECK            */
extern SemaphoreHandle_t emu_global_loop_start_semaphore;
extern SemaphoreHandle_t emu_global_loop_wtd_semaphore;
/*********************************************************/

/**********************************************************************/
/*          LIST OF BLOCKS AND THEIR FUCNTIONS TO RUN                 */
//To do consider hiding functions pointer in struct
uint16_t emu_body_block_structs_execution_list;
void** emu_global_blocks_structs=NULL;
emu_block_func *emu_body_functions_execution_list=NULL;
/**********************************************************************/

static emu_err_t emu_execute();

void emu_body_loop_task(void* params){
    while(1){
        if(pdTRUE == xSemaphoreTake(emu_global_loop_start_semaphore, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            emu_execute();
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %lld us, watchdog triggered: %d", (end_time - start_time), WTD_TRIGGERED());
            xSemaphoreGive(emu_global_loop_wtd_semaphore);
            taskYIELD();
        }
    }
}

static emu_err_t emu_execute(){
    static const char* _TAG = "EMU_BODY_EXECUTE";
    for (uint16_t i = 0; i < emu_body_block_structs_execution_list; i++) {
        LOG_I(_TAG, "Now will execute block %d", i);
        emu_err_t err = emu_body_functions_execution_list[i](emu_global_blocks_structs[i]);
        if (err != EMU_OK) {
            ESP_LOGE(_TAG, "Block %d failed during execution, error: %s", i, EMU_ERR_TO_STR(err));
            return err;
        }
    }
    return EMU_OK;
}

emu_err_t emu_body_create_execution_table(uint16_t total_blocks_cnt) {
    static const char* _TAG = "EMU_CREATE_BLOCK_TABLES";
    if (total_blocks_cnt == 0){
        ESP_LOGE(_TAG, "Zero blocks to create provided");
        return EMU_ERR_INVALID_ARG;
    }

    emu_body_block_structs_execution_list = total_blocks_cnt;
    // Allocate array of pointers to block_handle_t
    emu_global_blocks_structs = (void**)calloc(emu_body_block_structs_execution_list, sizeof(void*));
    if (!emu_global_blocks_structs) {
        ESP_LOGE(_TAG, "Failed to allocate space for block structs");
        return EMU_ERR_NO_MEMORY;
    }

    // Allocate array of function pointers
    emu_body_functions_execution_list = (emu_block_func*)calloc(emu_body_block_structs_execution_list, sizeof(emu_block_func));
    if (!emu_body_functions_execution_list) {
        free(emu_global_blocks_structs);
        emu_global_blocks_structs = NULL;
        ESP_LOGE(_TAG, "Failed to allocate space for blocks function pointers");
        return EMU_ERR_NO_MEMORY;
    }
    LOG_I(_TAG, "Successfully allocated space for %d block structs and functions", total_blocks_cnt);
    return EMU_OK;
}

void emu_block_free_all_blocks() {
    static const char* _TAG = "EMU_BLOCKS_FREE_ALL";
    if (emu_global_blocks_structs) {
        for (uint16_t i = 0; i < emu_body_block_structs_execution_list; i++) {
            if (emu_global_blocks_structs[i]) {
                free(emu_global_blocks_structs[i]); // free individual blocks if allocated
            }
        }
        free(emu_global_blocks_structs);
        emu_global_blocks_structs = NULL;
    }

    if (emu_body_functions_execution_list) {
        free(emu_body_functions_execution_list);
        emu_body_functions_execution_list = NULL;
    }

    emu_body_block_structs_execution_list = 0;
    LOG_I(_TAG, "Succesfully cleared all created blocks");
}
