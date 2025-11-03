#include "emulator_variables.h"
#include "emulator.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "DATAHOLDER";


emu_err_t emulator_dataholder_create(emu_mem_t *mem, emu_data_cnt_t *sizes)
{
    if (!mem || !sizes) return EMU_ERR_INVALID_ARG;
    (*mem) = (emu_mem_t){0};
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
    uint8_t bool_space =  (sizes->b + 7) / 8;

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

void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt){
      
      switch ((data_types_t)x) {
            case DATA_UI8:
            case DATA_I8:   
                (*total) += sizeof(uint8_t);
                break;
            case DATA_UI16:
            case DATA_I16:
                (*total) += sizeof(uint16_t);
                break;
            case DATA_UI32:
            case DATA_I32:
            case DATA_F:
                (*total) += sizeof(uint32_t);
                break;
            case DATA_UI64:
            case DATA_I64:
            case DATA_D:
                (*total) += sizeof(uint64_t);
                break;
            case DATA_B:
                (*bool_cnt) ++;
            default:
                break;
            (*total) += (*bool_cnt+ 7) / 8;
        }
}
