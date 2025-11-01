#include "emulator_variables.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "DATAHOLDER";

emulator_err_t emulator_dataholder_create(emu_mem_t *mem, const emu_size_t *sizes)
{
    if (!mem || !sizes) return EMU_ERR_INVALID_ARG;
    *mem = (emu_mem_t){0};

    ESP_LOGI(TAG, "Allocating dataholder");

    mem->i8  = calloc(sizes->i8, sizeof(int8_t));
    mem->i16 = calloc(sizes->i16, sizeof(int16_t));
    mem->i32 = calloc(sizes->i32, sizeof(int32_t));
    mem->i64 = calloc(sizes->i64, sizeof(int64_t));

    mem->u8  = calloc(sizes->u8, sizeof(uint8_t));
    mem->u16 = calloc(sizes->u16, sizeof(uint16_t));
    mem->u32 = calloc(sizes->u32, sizeof(uint32_t));
    mem->u64 = calloc(sizes->u64, sizeof(uint64_t));

    mem->f   = calloc(sizes->f, sizeof(float));
    mem->d   = calloc(sizes->d, sizeof(double));
    mem->b   = calloc(sizes->b, sizeof(bool));
    mem->custom = calloc(sizes->custom, sizeof(int16_t));

    // check for failure
    if (!mem->i8 || !mem->u8 || !mem->f) {
        ESP_LOGE(TAG, "Allocation failed");
        emulator_dataholder_free(mem);
        return EMU_ERR_NO_MEMORY;
    }

    ESP_LOGI(TAG, "Dataholder created successfully");
    return EMU_OK;
}


void emulator_dataholder_free(emu_mem_t *mem)
{
    if (!mem) return;

    free(mem->i8);   free(mem->i16);   free(mem->i32);   free(mem->i64);
    free(mem->u8);   free(mem->u16);   free(mem->u32);   free(mem->u64);
    free(mem->f);    free(mem->d);     free(mem->b);
    free(mem->custom);

    *mem = (emu_mem_t){0};
    ESP_LOGI(TAG, "Emulator memory freed");
}
