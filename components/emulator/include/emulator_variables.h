#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "emulator_types.h"
    
typedef struct {
    void     *_base_ptr; // Holds the pointer to the single allocated memory block
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

    // --- Parallel system for Multi-Dimensional (MD) variables ---
    void*           _md_base_ptr;  // Base pointer for the MD memory block.
    size_t          num_md_vars;   // The number of MD variables.
    struct emu_md_variable {
        data_types_t type;
        uint8_t      num_dims;
        size_t       dims[3];
        void*        data;         // Pointer to this variable's data within _md_base_ptr.
    } *md_vars;

}emu_mem_t;

extern emu_mem_t mem;

/**
* @brief return size of variable that data_types_t enum points at
*/
static inline size_t data_size(data_types_t t)
{
    switch (t) {
        case DATA_UI8:  case DATA_I8:   return sizeof(uint8_t);
        case DATA_UI16: case DATA_I16:  return sizeof(uint16_t);
        case DATA_UI32: case DATA_I32:  return sizeof(uint32_t);
        case DATA_UI64: case DATA_I64:  return sizeof(uint64_t);
        case DATA_F:                    return sizeof(float);
        case DATA_D:                    return sizeof(double);
        case DATA_B:                    return sizeof(bool);
        default:                        return 0;
    }
}

/**
* @brief return poiter to [type] value at [index]
*/
static inline void* mem_get_ptr(data_types_t type, size_t index) {
    // Bounds checking should be added here if array sizes are known
    switch (type) {
        case DATA_I8:   return &mem.i8[index];
        case DATA_I16:  return &mem.i16[index];
        case DATA_I32:  return &mem.i32[index];
        case DATA_I64:  return &mem.i64[index];
        case DATA_UI8:  return &mem.u8[index];
        case DATA_UI16: return &mem.u16[index];
        case DATA_UI32: return &mem.u32[index];
        case DATA_UI64: return &mem.u64[index];
        case DATA_F:    return &mem.f[index];
        case DATA_D:    return &mem.d[index];
        case DATA_B:    return &mem.b[index];
        default:        return NULL;
    }
}

/**
* @brief return c type form data_type_t
*/
#define C_TYPE_FROM_EMU_TYPE(d_type) \
    __typeof__( \
        (d_type) == DATA_I8   ? (int8_t)0   : \
        (d_type) == DATA_I16  ? (int16_t)0  : \
        (d_type) == DATA_I32  ? (int32_t)0  : \
        (d_type) == DATA_I64  ? (int64_t)0  : \
        (d_type) == DATA_UI8  ? (uint8_t)0  : \
        (d_type) == DATA_UI16 ? (uint16_t)0 : \
        (d_type) == DATA_UI32 ? (uint32_t)0 : \
        (d_type) == DATA_UI64 ? (uint64_t)0 : \
        (d_type) == DATA_F    ? (float)0.0f : \
        (d_type) == DATA_D    ? (double)0.0 : \
        (d_type) == DATA_B    ? (bool)false : \
        (int)0 \
    )

/**
* @brief retrieve value from index 
*/
#define MEM_GET(d_type, index) \
    ({ \
        C_TYPE_FROM_EMU_TYPE(d_type) val = 0; \
        void* ptr = mem_get_ptr(d_type, index); \
        if (ptr) { \
            memcpy(&val, ptr, data_size(d_type)); \
        } \
        val; \
    })

/**
* @brief write value at index
*/
#define MEM_SET(d_type, index, value_ptr) \
    do { \
        void* ptr = mem_get_ptr(d_type, index); \
        if (ptr) { \
            memcpy(ptr, (value_ptr), data_size(d_type)); \
        } \
    } while(0)

/**
 * @brief Gets a multi-dimensional variable by its index.
 * @param var_index The index of the variable in the descriptor array.
 * @return Pointer to the variable's runtime descriptor, or NULL if not found.
 */
static inline const struct emu_md_variable* get_md_variable(size_t var_index) {
    if (!mem.md_vars || var_index >= mem.num_md_vars) {
        return NULL;
    }
    return &mem.md_vars[var_index];
}

/**
 * @brief Gets a pointer to a specific element within a multi-dimensional array.
 * @param var Pointer to the variable descriptor.
 * @param ... Indices for each dimension. Must match var->num_dims.
 * @return A void pointer to the element, or NULL on error (e.g., out of bounds).
 */
void* mem_get_md_element_ptr(const struct emu_md_variable* var, ...);

/**
 * @brief Retrieves a value from a named multi-dimensional variable.
 * @param c_type The C type you expect to get back.
 * @param var_index The index of the variable.
 * @param ... The indices for the element (e.g., z, y, x).
 */
#define MEM_GET_MD(c_type, var_index, ...) \
    ({ \
        c_type val = 0; \
        const struct emu_md_variable* var = get_md_variable(var_index); \
        void* ptr = var ? mem_get_md_element_ptr(var, __VA_ARGS__) : NULL; \
        if (ptr) { memcpy(&val, ptr, sizeof(c_type)); } \
        val; \
    })

#define MEM_SET_MD(var_index, value_ptr, ...) \
    do { \
        const struct emu_md_variable* var = get_md_variable(var_index); \
        void* ptr = var ? mem_get_md_element_ptr(var, __VA_ARGS__) : NULL; \
        if (ptr) { memcpy(ptr, (value_ptr), data_size(var->type)); } \
    } while(0)

emu_err_t emulator_dataholder_create(emu_mem_t *mem, uint8_t *sizes);
void emulator_dataholder_free(emu_mem_t *mem);
emu_err_t emulator_md_dataholder_create(emu_mem_t *mem, const emu_md_variable_desc_t* descriptors, size_t num_descriptors);