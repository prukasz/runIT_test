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
    uint8_t  u8;
    uint8_t  u16;
    uint8_t  u32;
    uint8_t  u64;
    uint8_t  i8;
    uint8_t  i16;
    uint8_t  i32;
    uint8_t  i64;
    uint8_t  f;
    uint8_t  d;
    uint8_t  b;
}emu_data_cnt_t;

typedef struct {
    int8_t   *i8;
    int16_t  *i16;
    int32_t  *i32;
    int64_t  *i64;

    uint8_t  *u8;
    uint16_t *u16;
    uint32_t *u32;
    uint64_t *u64;

    float    *f;
    double   *d;
    bool     *b;
    int16_t  *custom;
}emu_mem_t;


void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt);
emu_err_t emulator_dataholder_create(emu_mem_t *mem, emu_data_cnt_t *sizes);
void emulator_dataholder_free(emu_mem_t *mem);