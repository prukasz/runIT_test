#include "emulator_variables.h"
#include "emulator.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "DATAHOLDER";

emu_err_t emulator_dataholder_create(emu_mem_t *mem, emu_size_t *sizes)
{
    if (!mem || !sizes) return EMU_ERR_INVALID_ARG;
    (*mem) = (emu_mem_t){0};
    mem->mem_size_8  = calloc(sizes->cnt_size_8,  sizeof(uint8_t ));
    mem->mem_size_16 = calloc(sizes->cnt_size_16, sizeof(uint16_t));
    mem->mem_size_32 = calloc(sizes->cnt_size_32, sizeof(uint32_t));
    mem->mem_size_64 = calloc(sizes->cnt_size_64, sizeof(uint64_t));
    uint8_t bool_space =  (sizes->cnt_size_1 + 7) / 8;
    mem->mem_size_1  = calloc(bool_space, sizeof(uint8_t));
    //todo check if allocated successfully
    ESP_LOGI(TAG, "Dataholder created successfully");
    return EMU_OK;
}

void emulator_dataholder_free(emu_mem_t *mem)
{
    if (!mem) return;

    free(mem->mem_size_8 );
    free(mem->mem_size_16);
    free(mem->mem_size_32);
    free(mem->mem_size_64);
    free(mem->mem_size_1 );

    (*mem)= (emu_mem_t){0};//clear pointers
    ESP_LOGI(TAG, "Dataholder memory freed");
}
