#include "emulator.h"
#include "emulator_loop.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"

static const char *TAG = "BODY TASK";
extern SemaphoreHandle_t loop_semaphore;
extern SemaphoreHandle_t wtd_semaphore;

uint16_t blocks_cnt;
block_handle_t** blocks_structs=NULL;
emu_block_func *blocks_table=NULL;
emu_err_t emu_execute();

void loop_task(void* params){
    while(1){
        if(pdTRUE == xSemaphoreTake(loop_semaphore, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            emu_execute();
            //ESP_LOGI(TAG, "folat at idx 1: %lf", mem_get_as_d(DATA_F, 0, (uint8_t[3]){0,255,255}));
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %lld us, watchdog triggered: %d", (end_time - start_time), status.wtd.wtd_triggered);
            xSemaphoreGive(wtd_semaphore);
            taskYIELD();
        }
    }
}

emu_err_t emu_execute(){
    for (uint16_t i = 0; i < blocks_cnt; i++) {
        emu_err_t err = blocks_table[i](blocks_structs[i]);
        if (err != EMU_OK) {
            ESP_LOGE("BLOCK", "Function %zu failed (err=%d)", i, err);
            return err;
        }
    }
    return EMU_OK;
}

emu_err_t emu_create_block_tables(uint16_t num_blocks) {
    if (num_blocks == 0) return EMU_ERR_INVALID_ARG;

    blocks_cnt = num_blocks;

    // Allocate array of pointers to block_handle_t
    blocks_structs = (block_handle_t**)calloc(blocks_cnt, sizeof(block_handle_t*));
    if (!blocks_structs) {
        return EMU_ERR_NO_MEMORY;
    }

    // Allocate array of function pointers
    blocks_table = (emu_block_func*)calloc(blocks_cnt, sizeof(emu_block_func));
    if (!blocks_table) {
        free(blocks_structs);
        blocks_structs = NULL;
        return EMU_ERR_NO_MEMORY;
    }

    return EMU_OK;
}

void emu_free_block_tables() {
    if (blocks_structs) {
        for (uint16_t i = 0; i < blocks_cnt; i++) {
            if (blocks_structs[i]) {
                free(blocks_structs[i]); // free individual blocks if allocated
            }
        }
        free(blocks_structs);
        blocks_structs = NULL;
    }

    if (blocks_table) {
        free(blocks_table);
        blocks_table = NULL;
    }

    blocks_cnt = 0;
}

emu_err_t emu_assign_block(uint16_t index, block_handle_t *block, emu_block_func func) {
    if (index >= blocks_cnt) {
        return EMU_ERR_INVALID_ARG;
    }
    blocks_structs[index] = block;
    blocks_table[index] = func;
    return EMU_OK;
}