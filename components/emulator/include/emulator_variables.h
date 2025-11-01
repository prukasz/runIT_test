#pragma once
#include "stdint.h"
#include "stdbool.h"
#include "emulator_loop.h"   // for emulator_err_t

// Type counts for each memory table
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
    uint8_t  custom;
}emu_size_t;

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

emulator_err_t emulator_dataholder_create(emu_mem_t *mem, const emu_size_t *sizes);
void emulator_dataholder_free(emu_mem_t *mem);

