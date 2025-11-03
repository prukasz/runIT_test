#include "emulator_variables.h"
#include "emulator.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "DATAHOLDER";


emu_err_t emulator_dataholder_create(emu_mem_t *mem, uint8_t *sizes)
{
    if (!mem || !sizes) return EMU_ERR_INVALID_ARG;
    (*mem) = (emu_mem_t){0};
    mem->i8  = calloc(sizes[0],  sizeof(int8_t));
    mem->i16 = calloc(sizes[1], sizeof(int16_t));
    mem->i32 = calloc(sizes[2], sizeof(int32_t));
    mem->i64 = calloc(sizes[3], sizeof(int64_t));

    mem->u8  = calloc(sizes[4], sizeof(uint8_t));
    mem->u16 = calloc(sizes[5], sizeof(uint16_t));
    mem->u32 = calloc(sizes[6], sizeof(uint32_t));
    mem->u64 = calloc(sizes[7], sizeof(uint64_t));

    mem->f   = calloc(sizes[8], sizeof(float));
    mem->d   = calloc(sizes[9], sizeof(double));
    uint8_t bool_space =  (sizes[10] + 7) / 8;

    mem->b  = calloc(bool_space, sizeof(uint8_t));
    //todo check if allocated successfully
    ESP_LOGI(TAG, "Dataholder created successfully");
    return EMU_OK;
}

void emulator_dataholder_free(emu_mem_t *mem)
{
    if (!mem) return;

    free(mem->i8);   free(mem->i16);   free(mem->i32);   free(mem->i64);
    free(mem->u8);   free(mem->u16);   free(mem->u32);   free(mem->u64);
    free(mem->f);    free(mem->d);     free(mem->b);

    (*mem)= (emu_mem_t){0};//clear pointers
    ESP_LOGI(TAG, "Dataholder memory freed");
}


