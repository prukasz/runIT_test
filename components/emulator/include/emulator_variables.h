#pragma once
#include "emulator.h"

#define GET_4BIT_FIELD(val, idx) (((val) >> ((idx) * 4)) & 0xF)
#define GET_DATA_TYPE(val, idx)  ((data_types_t)GET_4BIT_FIELD(val, idx))


typedef enum{
    DATA_UI8,
    DATA_UI16,
    DATA_UI32,
    DATA_UI64,
    DATA_I8,
    DATA_I16,
    DATA_I32,
    DATA_I64,
    DATA_F,
    DATA_D,
    DATA_B
}data_types_t;

typedef struct {
    uint8_t cnt_size_8;
    uint8_t cnt_size_16;
    uint8_t cnt_size_32;
    uint8_t cnt_size_64;
    uint8_t cnt_size_1;
}emu_size_t;

typedef struct {
    void * mem_size_8;
    void * mem_size_16;
    void * mem_size_32;
    void * mem_size_64;
    void * mem_size_1;
}emu_mem_t;


void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt);
emu_err_t emulator_dataholder_create(emu_mem_t *mem, emu_size_t *sizes);
void emulator_dataholder_free(emu_mem_t *mem);