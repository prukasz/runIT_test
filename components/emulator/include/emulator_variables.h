#pragma once
#include <stdbool.h>
#include <string.h>
#include "emulator_types.h"
#include "gatt_buff.h"
#include "esp_log.h"
/*here data for emulator is managed*/

/*single array sruct*/
#define DEFINE_ARR_TYPE(c_type, name) \
typedef struct { \
    uint8_t num_dims; \
    uint8_t dims[3]; \
    c_type *data; \
} arr_##name##_t;

DEFINE_ARR_TYPE(uint8_t,  ui8)
DEFINE_ARR_TYPE(uint16_t, ui16)
DEFINE_ARR_TYPE(uint32_t, ui32)
DEFINE_ARR_TYPE(int8_t,   i8)
DEFINE_ARR_TYPE(int16_t,  i16)
DEFINE_ARR_TYPE(int32_t,  i32)
DEFINE_ARR_TYPE(float,    f)
DEFINE_ARR_TYPE(double,   d)
DEFINE_ARR_TYPE(bool,     b)


/**
 * @brief this is memory struct for emulator
 */

typedef struct {
    void     *_base_ptr; // Scalars memory block
    int8_t   *i8;
    int16_t  *i16;
    int32_t  *i32;
    uint8_t  *u8;
    uint16_t *u16;
    uint32_t *u32;
    float    *f;
    double   *d;
    bool     *b;

    void  *_base_arr_ptr;
    void  *_base_arr_handle_ptr;
    arr_ui8_t  *arr_ui8;
    arr_ui16_t *arr_ui16;
    arr_ui32_t *arr_ui32;
    arr_i8_t   *arr_i8;
    arr_i16_t  *arr_i16;
    arr_i32_t  *arr_i32;
    arr_f_t    *arr_f;
    arr_d_t    *arr_d;
    arr_b_t    *arr_b;
} emu_mem_t;

/*THIS IS GLOBAL MEMORY STRUCT INSTANCE*/
extern emu_mem_t mem;
/*THIS IS GLOBAL MEMORY STRUCT INSTANCE*/

/*Return correct data size from enum*/
static inline size_t data_size(data_types_t type)
{
    switch (type) {
        case DATA_UI8:  case DATA_I8:   return sizeof(uint8_t);
        case DATA_UI16: case DATA_I16:  return sizeof(uint16_t);
        case DATA_UI32: case DATA_I32:  return sizeof(uint32_t);
        case DATA_F:                    return sizeof(float);
        case DATA_D:                    return sizeof(double);
        case DATA_B:                    return sizeof(bool);
        default:                        return 0;
    }
}

/*this macro returns index of variable in 1D table*/
#define _arr_flat_index(num_dims, dims, i0, i1, i2) ({            \
    size_t _idx = (size_t)-1;                                     \
    if ((num_dims) == 1) {                                        \
        _idx = ((i0) < (dims)[0]) ? (i0) : (size_t)-1;           \
    } else if ((num_dims) == 2) {                                 \
        _idx = ((i0) < (dims)[0] && (i1) < (dims)[1]) ?           \
               ((i0) + (i1) * (dims)[0]) : (size_t)-1;           \
    } else if ((num_dims) == 3) {                                 \
        _idx = ((i0) < (dims)[0] && (i1) < (dims)[1] && (i2) < (dims)[2]) ? \
               ((i0) + (i1) * (dims)[0] + (i2) * (dims)[0] * (dims)[1]) : (size_t)-1; \
    }                                                              \
    _idx;                                                          \
})


/**
 * @brief create arrays space
 */
emu_err_t emu_variables_create(emu_mem_t *mem, uint8_t *sizes);

/**
 * @brief create single variables space
 */
emu_err_t emu_arrays_create(chr_msg_buffer_t *source, emu_mem_t *mem, int start_index);

/**
* @brief reset memory
*/
void emu_variables_reset(emu_mem_t *mem);

/*this is internal macro*/
#define _MEM_GET(field, arr_field, var_idx, i0, i1, i2) \
    (((i0) == SIZE_MAX && (i1) == SIZE_MAX && (i2) == SIZE_MAX) ? \
        (mem.field[var_idx]) : \
        ({ \
            size_t _idx = _arr_flat_index(mem.arr_field[var_idx].num_dims, \
                                          mem.arr_field[var_idx].dims, \
                                          i0, i1, i2); \
            (_idx != (size_t)-1 ? mem.arr_field[var_idx].data[_idx] : 0); \
        }))

/*those are get macros SIZE_MAX to exclude index */
#define MEM_GET_I8(var_idx, i0, i1, i2)   _MEM_GET(i8,   arr_i8,   var_idx, i0, i1, i2)
#define MEM_GET_I16(var_idx, i0, i1, i2)  _MEM_GET(i16,  arr_i16,  var_idx, i0, i1, i2)
#define MEM_GET_I32(var_idx, i0, i1, i2)  _MEM_GET(i32,  arr_i32,  var_idx, i0, i1, i2)

#define MEM_GET_U8(var_idx, i0, i1, i2)   _MEM_GET(u8,   arr_ui8,  var_idx, i0, i1, i2)
#define MEM_GET_U16(var_idx, i0, i1, i2)  _MEM_GET(u16,  arr_ui16, var_idx, i0, i1, i2)
#define MEM_GET_U32(var_idx, i0, i1, i2)  _MEM_GET(u32,  arr_ui32, var_idx, i0, i1, i2)

#define MEM_GET_F(var_idx, i0, i1, i2)    _MEM_GET(f,    arr_f,    var_idx, i0, i1, i2)
#define MEM_GET_D(var_idx, i0, i1, i2)    _MEM_GET(d,    arr_d,    var_idx, i0, i1, i2)
#define MEM_GET_B(var_idx, i0, i1, i2)    _MEM_GET(b,    arr_b,    var_idx, i0, i1, i2)

/*this is universal set macro SIZE_MAX to exclude index*/
#define MEM_SET_DATATYPE(type, var_idx, i0, i1, i2, value) do {                 \
    if ((i0) == SIZE_MAX && (i1) == SIZE_MAX && (i2) == SIZE_MAX) {            \
        switch(type) {                                                         \
            case DATA_UI8:  mem.u8[var_idx]  = (uint8_t)(value); break;       \
            case DATA_UI16: mem.u16[var_idx] = (uint16_t)(value); break;      \
            case DATA_UI32: mem.u32[var_idx] = (uint32_t)(value); break;      \
            case DATA_I8:   mem.i8[var_idx]  = (int8_t)(value); break;        \
            case DATA_I16:  mem.i16[var_idx] = (int16_t)(value); break;       \
            case DATA_I32:  mem.i32[var_idx] = (int32_t)(value); break;       \
            case DATA_F:    mem.f[var_idx]   = (float)(value); break;         \
            case DATA_D:    mem.d[var_idx]   = (double)(value); break;        \
            case DATA_B:    mem.b[var_idx]   = (bool)(value); break;          \
            default: break;                                                    \
        }                                                                       \
    } else {                                                                    \
        size_t idx = (size_t)-1;                                               \
        switch(type) {                                                         \
            case DATA_UI8:   idx = _arr_flat_index(mem.arr_ui8[var_idx].num_dims, mem.arr_ui8[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_ui8[var_idx].data[idx] = (uint8_t)(value); } break; \
            case DATA_UI16:  idx = _arr_flat_index(mem.arr_ui16[var_idx].num_dims, mem.arr_ui16[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_ui16[var_idx].data[idx] = (uint16_t)(value); } break; \
            case DATA_UI32:  idx = _arr_flat_index(mem.arr_ui32[var_idx].num_dims, mem.arr_ui32[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_ui32[var_idx].data[idx] = (uint32_t)(value); } break; \
            case DATA_I8:    idx = _arr_flat_index(mem.arr_i8[var_idx].num_dims, mem.arr_i8[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_i8[var_idx].data[idx] = (int8_t)(value); } break; \
            case DATA_I16:   idx = _arr_flat_index(mem.arr_i16[var_idx].num_dims, mem.arr_i16[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_i16[var_idx].data[idx] = (int16_t)(value); } break; \
            case DATA_I32:   idx = _arr_flat_index(mem.arr_i32[var_idx].num_dims, mem.arr_i32[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_i32[var_idx].data[idx] = (int32_t)(value); } break; \
            case DATA_F:     idx = _arr_flat_index(mem.arr_f[var_idx].num_dims, mem.arr_f[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_f[var_idx].data[idx] = (float)(value); } break; \
            case DATA_D:     idx = _arr_flat_index(mem.arr_d[var_idx].num_dims, mem.arr_d[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_d[var_idx].data[idx] = (double)(value); } break; \
            case DATA_B:     idx = _arr_flat_index(mem.arr_b[var_idx].num_dims, mem.arr_b[var_idx].dims, i0,i1,i2); \
                                if(idx != (size_t)-1) { mem.arr_b[var_idx].data[idx] = (bool)(value); } break; \
            default: break;                                                     \
        }                                                                       \
    }                                                                           \
} while(0)

