#pragma once 
#include "emu_variables.h"
#include "emu_logging.h"
#include <stdint.h>
#include <math.h>
#include <stdbool.h>


/**
 * @brief Get mem_var_t from provided mem_access_t node
 * @param result mem_var_t for resut to be stored
 * @param search mem_access_t node with wanted instance
 * @param by_reference mem_var_t ptr at data_pool when true
 * @return emu_result_t .code == EMU_OK when success
 * @note Type returned depends on instance data type
 */
emu_result_t mem_get(mem_var_t *result, const mem_access_t *search, bool by_reference);


/**
 * @brief Set value from mem_vat_t struct into instance provided by 
 * @param source mem_var_t with val to set
 * @param target mem_access_t providnig target instance
 * @return emu_result_t EMU_OK when success
 * @note This will perform ceil / floor / round when type mismatch
 */
emu_result_t mem_set(const mem_var_t source, const mem_access_t *target);


/**
 * @brief Parse and create space for access structs
 * @param data packet buff (skip header)
 * @param el_cnt packet buff len (-header len)
 * @param nothing pass NULL
 * @return emu_result_t.code = EMU_OK when success, else look emu_result_t struct def
 * @note Packet [uint16_t ref_cnt][uint16_t total_indices]
 * @details Access space is common for all contexts,
 * Total_indices value is computed as sum of provided static/dynamic indices_values in each struct
 */
emu_result_t emu_mem_parse_access_create(const uint8_t*data, const uint16_t el_cnt, void* nothing);

/**
* @brief Free space for mem_access storage
*/
void mem_access_free_space(void);

/**
 * @brief Parse one access message (recursive if in need)
 */
emu_err_t emu_mem_parse_access(const uint8_t *data, const uint16_t el_cnt, uint16_t* idx, mem_access_t **out_ptr);

emu_result_t mem_access_allocate_space(uint16_t references_count, uint16_t total_indices);

/**
 * @brief Get value from mem_var_t when type known
 */
#define GET_VAL(v, field) ((v).by_reference ? *((v).data.ptr.field) : (v).data.val.field)


#define CLAMP_CAST(VAL, MIN, MAX, TYPE) ({ \
    __typeof__(VAL) _v = (VAL); \
    /* Compile-time dispatch: Only round if input is float */ \
    _v = __builtin_types_compatible_p(__typeof__(_v), float) ? roundf((float)_v) : _v; \
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
        case MEM_U8:  return CLAMP_CAST(GET_VAL(v, u8),  T_MIN, T_MAX, T_TYPE); \
        case MEM_U16: return CLAMP_CAST(GET_VAL(v, u16), T_MIN, T_MAX, T_TYPE); \
        case MEM_U32: return CLAMP_CAST(GET_VAL(v, u32), T_MIN, T_MAX, T_TYPE); \
        case MEM_I16:  return CLAMP_CAST(GET_VAL(v, i16), T_MIN, T_MAX, T_TYPE); \
        case MEM_I32:  return CLAMP_CAST(GET_VAL(v, i32), T_MIN, T_MAX, T_TYPE); \
        case MEM_F:    return CLAMP_CAST(GET_VAL(v, f),   T_MIN, T_MAX, T_TYPE); \
        case MEM_B:    return GET_VAL(v, b) ? (T_TYPE)1 : (T_TYPE)0; \
        default: return 0; \
    } \
}

// Instantiate Integer Converters
DEFINE_INT_CONVERTER(u8,  uint8_t,  0,         UINT8_MAX)
DEFINE_INT_CONVERTER(u16, uint16_t, 0,         UINT16_MAX)
DEFINE_INT_CONVERTER(u32, uint32_t, 0,         UINT32_MAX)
DEFINE_INT_CONVERTER(i16, int16_t,  INT16_MIN, INT16_MAX)
DEFINE_INT_CONVERTER(i32, int32_t,  INT32_MIN, INT32_MAX)

/* -------------------------------------------------------------------------- */
/* FLOAT / DOUBLE / BOOL IMPL                          */
/* -------------------------------------------------------------------------- */

static __always_inline float emu_var_to_f(mem_var_t v) {
    switch (v.type) {
        case MEM_F:    return GET_VAL(v, f);
        case MEM_U8:  return (float)GET_VAL(v, u8);
        case MEM_U16: return (float)GET_VAL(v, u16);
        case MEM_U32: return (float)GET_VAL(v, u32);
        case MEM_I16:  return (float)GET_VAL(v, i16);
        case MEM_I32:  return (float)GET_VAL(v, i32);
        case MEM_B:    return GET_VAL(v, b) ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}


 static __always_inline bool emu_var_to_b(mem_var_t v) {
    switch (v.type) {
        case MEM_B:    return GET_VAL(v, b);
        case MEM_F:    return (GET_VAL(v, f) != 0.0f);
        case MEM_U8:  return (GET_VAL(v, u8) != 0);
        case MEM_U16: return (GET_VAL(v, u16) != 0);
        case MEM_U32: return (GET_VAL(v, u32) != 0);
        case MEM_I16:  return (GET_VAL(v, i16) != 0);
        case MEM_I32:  return (GET_VAL(v, i32) != 0);
        default: return false;
    }
}


#define MEM_CAST(var, type_placeholder) _Generic((type_placeholder),\
    uint8_t:  emu_var_to_u8,  \
    uint16_t: emu_var_to_u16, \
    uint32_t: emu_var_to_u32, \
    int16_t:  emu_var_to_i16, \
    int32_t:  emu_var_to_i32, \
    float:    emu_var_to_f,   \
    bool:     emu_var_to_b,   \
    default:  emu_var_to_i32  \
)(var)


/**
 * @brief Get from provided mem_access structure value into chosen C-type
 * @param dst_ptr pointer at wanted type
 * @param mem_access_t_ptr what we want to get
 * @return emu_result_t from mem_get
 */

#define MEM_GET(dst_ptr, mem_access_t_ptr) ({ \
    const mem_access_t *_ma = (mem_access_t_ptr); \
    emu_result_t _res = {.code = EMU_OK}; \
    \
    /* Ultra-fast path: resolved index + matching type = direct array access */ \
    if (likely(_ma->is_index_resolved)) { \
        mem_instance_t *_inst = _ma->instance; \
        uint16_t _idx = _ma->resolved_index; \
        \
        /* Type dispatch: check if dst type matches instance type */ \
        if (_Generic(*(dst_ptr), \
            uint8_t:  (_inst->type == MEM_U8), \
            uint16_t: (_inst->type == MEM_U16), \
            uint32_t: (_inst->type == MEM_U32), \
            int16_t:  (_inst->type == MEM_I16), \
            int32_t:  (_inst->type == MEM_I32), \
            float:    (_inst->type == MEM_F), \
            bool:     (_inst->type == MEM_B), \
            default:  0 \
        )) { \
            /* Direct memory access - no function call overhead */ \
            *(dst_ptr) = _Generic(*(dst_ptr), \
                uint8_t:  _inst->data.u8[_idx], \
                uint16_t: _inst->data.u16[_idx], \
                uint32_t: _inst->data.u32[_idx], \
                int16_t:  _inst->data.i16[_idx], \
                int32_t:  _inst->data.i32[_idx], \
                float:    _inst->data.f[_idx], \
                bool:     _inst->data.b[_idx], \
                default:  0 \
            ); \
        } else { \
            /* Type conversion needed - use standard path */ \
            mem_var_t _tmp_var = {0}; \
            _res = mem_get(&_tmp_var, _ma, false); \
            if (_res.code == EMU_OK) { \
                *(dst_ptr) = MEM_CAST(_tmp_var, (__typeof__(*(dst_ptr)))0); \
            } \
        } \
    } else { \
        /* Dynamic index - use standard path */ \
        mem_var_t _tmp_var = {0}; \
        _res = mem_get(&_tmp_var, _ma, false); \
        if (_res.code == EMU_OK) { \
            *(dst_ptr) = MEM_CAST(_tmp_var, (__typeof__(*(dst_ptr)))0); \
        } \
    } \
    _res; \
})

