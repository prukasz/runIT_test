#pragma once
#include "emulator_types.h"
#include <stdint.h>
#include <math.h>
#include "mem_types.h"
bool parse_check_header(uint8_t *data, packet_header_t header);


static inline uint16_t parse_get_u16(const uint8_t *data, uint16_t offset) {
    uint16_t tmp;
    memcpy(&tmp, data + offset, sizeof(uint16_t));
    return tmp;
}

static inline int16_t parse_get_i16(const uint8_t *data, uint16_t offset) {
    int16_t tmp;
    memcpy(&tmp, data + offset, sizeof(int16_t));
    return tmp;
}

static inline uint32_t parse_get_u32(const uint8_t *data, uint16_t offset) {
    uint32_t tmp;
    memcpy(&tmp, data + offset, sizeof(uint32_t));
    return tmp;
}

static inline int32_t parse_get_i32(const uint8_t *data, uint16_t offset) {
    int32_t tmp;
    memcpy(&tmp, data + offset, sizeof(int32_t));
    return tmp;
}

static inline float parse_get_f(const uint8_t *data, uint16_t offset) {
    float tmp;
    memcpy(&tmp, data + offset, sizeof(float));
    return tmp;
}

static inline double parse_get_d(const uint8_t *data, uint16_t offset) {
    double tmp;
    memcpy(&tmp, data + offset, sizeof(double));
    return tmp;
}


/**
 * @brief Get value from mem_var_t when type known
 */
#define GET_VAL(v, field) ((v).by_reference ? *(v).data.ptr.field : (v).data.val.field)

// Robust Clamp with Rounding for Float/Double -> Int
#define CLAMP_CAST(VAL, MIN, MAX, TYPE) ({ \
    __typeof__(VAL) _v = (VAL); \
    /* Compile-time dispatch: Only round if input is float/double */ \
    if (__builtin_types_compatible_p(__typeof__(_v), float))       _v = roundf((float)_v); \
    else if (__builtin_types_compatible_p(__typeof__(_v), double)) _v = round((double)_v); \
    /* Disable type-limit warnings for generic limits (e.g., u8 < 0) */ \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wtype-limits\"") \
    TYPE _res = (_v < (MIN)) ? (TYPE)(MIN) : ((_v > (MAX)) ? (TYPE)(MAX) : (TYPE)_v); \
    _Pragma("GCC diagnostic pop") \
    _res; \
})

/* -------------------------------------------------------------------------- */
/* CONVERTER GENERATORS                            */
/* -------------------------------------------------------------------------- */

#define DEFINE_INT_CONVERTER(T_NAME, T_TYPE, T_MIN, T_MAX) \
static __always_inline T_TYPE emu_var_to_##T_NAME(mem_var_t v) { \
    switch (v.type) { \
        case DATA_U8:  return CLAMP_CAST(GET_VAL(v, u8),  T_MIN, T_MAX, T_TYPE); \
        case DATA_U16: return CLAMP_CAST(GET_VAL(v, u16), T_MIN, T_MAX, T_TYPE); \
        case DATA_U32: return CLAMP_CAST(GET_VAL(v, u32), T_MIN, T_MAX, T_TYPE); \
        case DATA_I8:   return CLAMP_CAST(GET_VAL(v, i8),  T_MIN, T_MAX, T_TYPE); \
        case DATA_I16:  return CLAMP_CAST(GET_VAL(v, i16), T_MIN, T_MAX, T_TYPE); \
        case DATA_I32:  return CLAMP_CAST(GET_VAL(v, i32), T_MIN, T_MAX, T_TYPE); \
        case DATA_F:    return CLAMP_CAST(GET_VAL(v, f),   T_MIN, T_MAX, T_TYPE); \
        case DATA_D:    return CLAMP_CAST(GET_VAL(v, d),   T_MIN, T_MAX, T_TYPE); \
        case DATA_B:    return GET_VAL(v, b) ? (T_TYPE)1 : (T_TYPE)0; \
        default: return 0; \
    } \
}

// Instantiate Integer Converters
DEFINE_INT_CONVERTER(u8,  uint8_t,  0,         UINT8_MAX)
DEFINE_INT_CONVERTER(u16, uint16_t, 0,         UINT16_MAX)
DEFINE_INT_CONVERTER(u32, uint32_t, 0,         UINT32_MAX)
DEFINE_INT_CONVERTER(i8,  int8_t,   INT8_MIN,  INT8_MAX)
DEFINE_INT_CONVERTER(i16, int16_t,  INT16_MIN, INT16_MAX)
DEFINE_INT_CONVERTER(i32, int32_t,  INT32_MIN, INT32_MAX)

/* -------------------------------------------------------------------------- */
/* FLOAT / DOUBLE / BOOL IMPL                          */
/* -------------------------------------------------------------------------- */

static __always_inline float emu_var_to_f(mem_var_t v) {
    switch (v.type) {
        case DATA_F:    return GET_VAL(v, f);
        case DATA_D:    return (float)GET_VAL(v, d);
        case DATA_U8:  return (float)GET_VAL(v, u8);
        case DATA_U16: return (float)GET_VAL(v, u16);
        case DATA_U32: return (float)GET_VAL(v, u32);
        case DATA_I8:   return (float)GET_VAL(v, i8);
        case DATA_I16:  return (float)GET_VAL(v, i16);
        case DATA_I32:  return (float)GET_VAL(v, i32);
        case DATA_B:    return GET_VAL(v, b) ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

static __always_inline double emu_var_to_d(mem_var_t v) {
    switch (v.type) {
        case DATA_D:    return GET_VAL(v, d);
        case DATA_F:    return (double)GET_VAL(v, f);
        case DATA_U8:  return (double)GET_VAL(v, u8);
        case DATA_U16: return (double)GET_VAL(v, u16);
        case DATA_U32: return (double)GET_VAL(v, u32);
        case DATA_I8:   return (double)GET_VAL(v, i8);
        case DATA_I16:  return (double)GET_VAL(v, i16);
        case DATA_I32:  return (double)GET_VAL(v, i32);
        case DATA_B:    return GET_VAL(v, b) ? 1.0 : 0.0;
        default: return 0.0;
    }
}

 static __always_inline bool emu_var_to_b(mem_var_t v) {
    switch (v.type) {
        case DATA_B:    return GET_VAL(v, b);
        case DATA_F:    return (GET_VAL(v, f) != 0.0f);
        case DATA_D:    return (GET_VAL(v, d) != 0.0);
        case DATA_U8:  return (GET_VAL(v, u8) != 0);
        case DATA_U16: return (GET_VAL(v, u16) != 0);
        case DATA_U32: return (GET_VAL(v, u32) != 0);
        case DATA_I8:   return (GET_VAL(v, i8) != 0);
        case DATA_I16:  return (GET_VAL(v, i16) != 0);
        case DATA_I32:  return (GET_VAL(v, i32) != 0);
        default: return false;
    }
}


#define MEM_CAST(var, type_placeholder) _Generic((type_placeholder),\
    uint8_t:  emu_var_to_u8,  \
    uint16_t: emu_var_to_u16, \
    uint32_t: emu_var_to_u32, \
    int8_t:   emu_var_to_i8,  \
    int16_t:  emu_var_to_i16, \
    int32_t:  emu_var_to_i32, \
    float:    emu_var_to_f,   \
    double:   emu_var_to_d,   \
    bool:     emu_var_to_b,   \
    default:  emu_var_to_i32  \
)(var)