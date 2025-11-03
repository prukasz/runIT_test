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
}emu_mem_t;

extern emu_mem_t mem;
#define MEM_GET(type, index) \
    ( \
        (type) == DATA_I8     ? (mem.i8[(index)])   : \
        (type) == DATA_I16    ? (mem.i16[(index)])  : \
        (type) == DATA_I32    ? (mem.i32[(index)])  : \
        (type) == DATA_I64    ? (mem.i64[(index)])  : \
        (type) == DATA_UI8    ? (mem.u8[(index)])   : \
        (type) == DATA_UI16   ? (mem.u16[(index)])  : \
        (type) == DATA_UI32   ? (mem.u32[(index)])  : \
        (type) == DATA_UI64   ? (mem.u64[(index)])  : \
        (type) == DATA_F      ? (mem.f[(index)])    : \
        (type) == DATA_D      ? (mem.d[(index)])    : \
        (type) == DATA_B      ? (mem.b[(index)])    : \
        0 \
    )

    #define MEM_SET(type, index, value) do { \
        if      ((type) == DATA_I8)    (mem.i8[(index)])  = (int8_t)(value); \
        else if ((type) == DATA_I16)   (mem.i16[(index)]) = (int16_t)(value); \
        else if ((type) == DATA_I32)   (mem.i32[(index)]) = (int32_t)(value); \
        else if ((type) == DATA_I64)   (mem.i64[(index)]) = (int64_t)(value); \
        else if ((type) == DATA_UI8)   (mem.u8[(index)])  = (uint8_t)(value); \
        else if ((type) == DATA_UI16)  (mem.u16[(index)]) = (uint16_t)(value); \
        else if ((type) == DATA_UI32)  (mem.u32[(index)]) = (uint32_t)(value); \
        else if ((type) == DATA_UI64)  (mem.u64[(index)]) = (uint64_t)(value); \
        else if ((type) == DATA_F)     (mem.f[(index)])   = (float)(value); \
        else if ((type) == DATA_D)     (mem.d[(index)])   = (double)(value); \
        else if ((type) == DATA_B)     (mem.b[(index)])   = (bool)(value); \
    } while(0)

void check_size(uint8_t x, uint16_t *total, uint8_t*bool_cnt);
emu_err_t emulator_dataholder_create(emu_mem_t *mem, uint8_t *sizes);
void emulator_dataholder_free(emu_mem_t *mem);