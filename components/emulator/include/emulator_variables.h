#pragma once
#include <stdbool.h>
#include <string.h>
#include "emulator_types.h"
#include "gatt_buff.h"


/** 
*@brief This macro generate arrays of chosen datatype used in emulator global memory
*/
#define _DEFINE_ARR_TYPE(c_type, name) \
typedef struct { \
    uint8_t num_dims; \
    uint8_t dims[3]; \
    c_type *data; \
} arr_##name##_t;
_DEFINE_ARR_TYPE(uint8_t,  ui8)
_DEFINE_ARR_TYPE(uint16_t, ui16)
_DEFINE_ARR_TYPE(uint32_t, ui32)
_DEFINE_ARR_TYPE(int8_t,   i8)
_DEFINE_ARR_TYPE(int16_t,  i16)
_DEFINE_ARR_TYPE(int32_t,  i32)
_DEFINE_ARR_TYPE(float,    f)
_DEFINE_ARR_TYPE(double,   d)
/**
* @brief Reset all parse guard flags 
*/
_DEFINE_ARR_TYPE(bool,     b)

typedef struct {
    //Common pointer for all "single varaibles"
    void     *_base_ptr;
    //pointer to an array of singe variables type: int8_t
    int8_t   *i8;
    //pointer to an array of singe variables type: int16_t
    int16_t  *i16;
    //pointer to an array of singe variables type: int32_t
    int32_t  *i32;
    //pointer to an array of singe variables type: uint8_t
    uint8_t  *u8;
    //pointer to an array of singe variables type: uint16_t
    uint16_t *u16;
    //pointer to an array of singe variables type: uint32_t
    uint32_t *u32;
    //pointer to an array of singe variables type: float
    float    *f;
    //pointer to an array of singe variables type: double
    double   *d;
    //pointer to an array of singe variables type: bool
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

/**
 * @brief create arrays space
 * @param mem pointer to global memory struct
 * @param types_cnt_table poiter to count of sinle varaible of each type
 */
emu_err_t emu_variables_single_create(emu_mem_t *mem, uint8_t *types_cnt_table);

/**
 * @brief create arrays space
 * @param mem pointer to global memory struct
 * @param start_index starting index in source buffer
 */
emu_err_t emu_variables_arrays_create(chr_msg_buffer_t *source, emu_mem_t *mem, uint16_t start_index);

/**
* @brief reset created memory
*/
void emu_variables_reset(emu_mem_t *mem);

/**
 * @brief Returns size of variables in bytes
 * @param type variable type 
 */
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

/**
 * @brief Returns relative index of chosen variable in it's flattened table
 * @param num_dims dimensions count of array
 * @param dims_table table of array sizes
 * @param idx_table target index
 * @return Flat index or "-1" if index doesn't exist
 */
#define _ARR_FLAT_IDX(num_dims, dims_table, idx_table) ({     \
    size_t _idx = (size_t)-1;                                     \
    if ((num_dims) == 1) {                                        \
        _idx = ((idx_table[0]) < (dims_table)[0]) ? (idx_table[0]) : (size_t)-1; \
    } else if ((num_dims) == 2) {                                 \
        _idx = ((idx_table[0]) < (dims_table)[0] && (idx_table[1]) < (dims_table)[1]) ? \
               ((idx_table[1]) + (idx_table[0]) * (dims_table)[1]) : (size_t)-1; \
    } else if ((num_dims) == 3) {                                 \
        _idx = ((idx_table[0]) < (dims_table)[0] && (idx_table[1]) < (dims_table)[1] && (idx_table[2]) < (dims_table)[2]) ? \
               ((idx_table[2]) + (idx_table[1]) * (dims_table)[2] + (idx_table[0]) * (dims_table)[1] * (dims_table)[2]) : (size_t)-1; \
    }                                                              \
    _idx;                                                          \
})

/*this is internal macro for fetching value from memeory*/
#define _MEM_GET(field, arr_field, var_idx, idx_table) \
    (((idx_table[0]) == UINT8_MAX && (idx_table[1]) == UINT8_MAX && (idx_table[2]) == UINT8_MAX) ? \
        (mem.field[var_idx]) : \
        ({ \
            size_t _idx = _ARR_FLAT_IDX(mem.arr_field[var_idx].num_dims, \
                                          mem.arr_field[var_idx].dims, idx_table); \
            (_idx != (size_t)-1 ? mem.arr_field[var_idx].data[_idx] : 0); \
        }))


/**
 * @brief Get any global varaible as double
 * @param type type of accesed variable
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
double mem_get_as_d(data_types_t type, size_t var_idx, uint8_t idx_table[3]);        
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_I8(var_idx, idx_table)   _MEM_GET(i8,   arr_i8,   var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_I16(var_idx, idx_table)  _MEM_GET(i16,  arr_i16,  var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_I32(var_idx, idx_table)  _MEM_GET(i32,  arr_i32,  var_idx, idx_table)

/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_U8(var_idx, idx_table)   _MEM_GET(u8,   arr_ui8,  var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_U16(var_idx, idx_table)  _MEM_GET(u16,  arr_ui16, var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_U32(var_idx, idx_table)  _MEM_GET(u32,  arr_ui32, var_idx, idx_table)

/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_F(var_idx, idx_table)    _MEM_GET(f,    arr_f,    var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_D(var_idx, idx_table)    _MEM_GET(d,    arr_d,    var_idx, idx_table)
/**
 * @brief Return value from table or single variable from chosen type
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_GET_B(var_idx, idx_table)    _MEM_GET(b,    arr_b,    var_idx, idx_table)


/*
Todo: rework so it caps value or round it
*/
/**
 * @brief Set varaible to chosen value
 * @param type type of variable accesed
 * @param var_idx index of single variable or table
 * @param idx_table indices to acces from table
 * @note This macros cast value if it is to big 
 * @note 0xFF idx is reserved for dimensional exclusion
 */
#define MEM_SET(type, var_idx, idx_table, value) ({                 \
    if ((idx_table[0]) == UINT8_MAX && (idx_table[1]) == UINT8_MAX && (idx_table[2]) == UINT8_MAX) {\
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
            case DATA_UI8:   idx = _ARR_FLAT_IDX(mem.arr_ui8[var_idx].num_dims, mem.arr_ui8[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_ui8[var_idx].data[idx] = (uint8_t)(value); } break; \
            case DATA_UI16:  idx = _ARR_FLAT_IDX(mem.arr_ui16[var_idx].num_dims, mem.arr_ui16[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_ui16[var_idx].data[idx] = (uint16_t)(value); } break; \
            case DATA_UI32:  idx = _ARR_FLAT_IDX(mem.arr_ui32[var_idx].num_dims, mem.arr_ui32[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_ui32[var_idx].data[idx] = (uint32_t)(value); } break; \
            case DATA_I8:    idx = _ARR_FLAT_IDX(mem.arr_i8[var_idx].num_dims, mem.arr_i8[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_i8[var_idx].data[idx] = (int8_t)(value); } break; \
            case DATA_I16:   idx = _ARR_FLAT_IDX(mem.arr_i16[var_idx].num_dims, mem.arr_i16[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_i16[var_idx].data[idx] = (int16_t)(value); } break; \
            case DATA_I32:   idx = _ARR_FLAT_IDX(mem.arr_i32[var_idx].num_dims, mem.arr_i32[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_i32[var_idx].data[idx] = (int32_t)(value); } break; \
            case DATA_F:     idx = _ARR_FLAT_IDX(mem.arr_f[var_idx].num_dims, mem.arr_f[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_f[var_idx].data[idx] = (float)(value); } break; \
            case DATA_D:     idx = _ARR_FLAT_IDX(mem.arr_d[var_idx].num_dims, mem.arr_d[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_d[var_idx].data[idx] = (double)(value); } break; \
            case DATA_B:     idx = _ARR_FLAT_IDX(mem.arr_b[var_idx].num_dims, mem.arr_b[var_idx].dims,idx_table); \
                                if(idx != (size_t)-1) { mem.arr_b[var_idx].data[idx] = (bool)(value); } break; \
            default: break;                                                     \
        }                                                                       \
    }                                                                           \
})

/******************************************************************************************************** 
*   VARIABLE ACCES AND USAGE 
*   THERE IS 9 TYPES OF VARIABLES (data_types_t)
*       UNIT/INT8---UINT/INT32 FLOAT DOUBLE AND BOOL
*   AND 4 TYPES OF STORAGE OPTIONS
*       SINGLE VARIABLE 1/2/3D TABLES
*   
*   EACH TYPE OF VARIABLE AND STORAGE OPTION HAS ITS OWN INDEX 
*       INDICES ARE GIVEN IN ORDER OF ALLOCATION TO EACH 'TABLE' STORING VARAIBLE 0->254 
*       INDEX 255 (0xFF) IS RESERVED
*
*   1/2/3D TABLE HAS STATIC SIZES DEFINED WHEN PARSED  
*       SIZE CAN VARY FROM (1-255), INDICES(0-254)ARE ALLOWED, 0->255 IS RESERVED
*   
*  ACCESING VARIABLES
*       DATA_TYPE_T TELLS WHERE TO SEARCH FOR VARIABLE, VAR_INDEX TELLS WHAT INDEX REFFER TO, 
*       IDX_TABLE[3] DETERMINE WHAT STORAGE TYPE OF VARIABLE IT IS AND PROVIDES INDICES OF TABLE
*       INDEX 255 (0xFF) MEANS EXCLUSION OF TABLE DIMENSION [0,0,255] MEANS VAR_INDEX[0][0], 
*       [255, 255, 255] MEANS ACCESS TO SINGLE VARIABLE WITH VAR_INDEX
*  
*       double mem_get_as_d(data_types_t type, size_t var_idx, uint8_t idx_table[3])
*       RETURNS VARIABLE CASTED TO DOUBLE FOR CONVINIENCE
*       MEM_GET_XX(var_idx, idx_table)
*       RETURNS TYPE (XX) VARIABLE ACCESED
*      
*       MEM_SET(type, var_idx, idx_table, value)
*       SET CHOSEN type VARIABLE TO VALUE (cast is done autamatically depending on type)
*       idx_table works as before to switch storage type
*
* OUT OF BOUNDARIES SITUATION
*       returns 0.0 or 0
****************************************************************************************************/