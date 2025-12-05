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
uint16_t emu_global_blocks_cnt;
void** emu_global_blocks_structs=NULL;
emu_block_func *emu_global_blocks_functions=NULL;
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
    for (uint16_t i = 0; i < emu_global_blocks_cnt; i++) {
        emu_err_t err = emu_global_blocks_functions[i](emu_global_blocks_structs[i]);
        if (err != EMU_OK) {
            ESP_LOGE(TAG, "Function %d failed (err: %d)", i, err);
            return err;
        }
    }
    return EMU_OK;
}

emu_err_t emu_create_block_tables(uint16_t num_blocks) {
    if (num_blocks == 0){
        return EMU_ERR_INVALID_ARG;
    }

    emu_global_blocks_cnt = num_blocks;
    // Allocate array of pointers to block_handle_t
    emu_global_blocks_structs = (void**)calloc(emu_global_blocks_cnt, sizeof(void*));
    if (!emu_global_blocks_structs) {
        return EMU_ERR_NO_MEMORY;
    }

    // Allocate array of function pointers
    emu_global_blocks_functions = (emu_block_func*)calloc(emu_global_blocks_cnt, sizeof(emu_block_func));
    if (!emu_global_blocks_functions) {
        free(emu_global_blocks_structs);
        emu_global_blocks_structs = NULL;
        return EMU_ERR_NO_MEMORY;
    }
    return EMU_OK;
}

void emu_free_block_tables() {
    if (emu_global_blocks_structs) {
        for (uint16_t i = 0; i < emu_global_blocks_cnt; i++) {
            if (emu_global_blocks_structs[i]) {
                free(emu_global_blocks_structs[i]); // free individual blocks if allocated
            }
        }
        free(emu_global_blocks_structs);
        emu_global_blocks_structs = NULL;
    }

    if (emu_global_blocks_functions) {
        free(emu_global_blocks_functions);
        emu_global_blocks_functions = NULL;
    }

    emu_global_blocks_cnt = 0;
}

emu_err_t emu_assign_block(uint16_t index, block_handle_t *block, emu_block_func func) {
    if (index >= emu_global_blocks_cnt) {
        return EMU_ERR_INVALID_ARG;
    }
    emu_global_blocks_structs[index] = block;
    emu_global_blocks_functions[index] = func;
    return EMU_OK;
}