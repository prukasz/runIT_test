#pragma once 
#include "stdint.h"
#include "stdbool.h"
#include "emulator_errors.h"
#include "emulator_variables.h"

typedef struct block_handle_s block_handle_t;


/**
 * @brief This index union represent one index value of array. 
 * It can be static value (Provided in emulator) or can refer to other instance as value
 */
typedef union {
        uint16_t static_idx;
        void* next_instance; 
} idx_u;

/**
 * @brief This struct is used in inputs and outputs of blocks
 * 
 */
typedef struct __attribute__((packed)) {
        uint8_t  dims_cnt     :4; //total count of array dimensions 0 is "scalar" when scalar we switch to acces_arr_t
        uint8_t  target_type  :4; //C-type target type (look at emu_types.h)
        uint8_t  context_id   :3; //This id allows for usage of multiple mem structs (contexts) (0 is global 1 is outputs of blocks)
        uint8_t  is_resolved  :1; //If "1" then direct poiner is avaible to memory, else need to search "recursive"
        uint8_t _reserved     :4; 
        uint16_t instance_idx;      //Target instance index (used when no direct avaible or when we want to search for instance struct)
        union{
            uint8_t*    static_ui8;
            uint16_t*   static_ui16;
            uint32_t*   static_ui32;
            int8_t*     static_i8;
            int16_t*    static_i16;
            int32_t*    static_i32;
            float*      static_f;
            double*     static_d;
            bool*       static_b;
        }direct_ptr;       
} mem_access_s_t;

typedef struct __attribute__((packed)) { 
    uint8_t  dims_cnt     :4; 
    uint8_t  target_type  :4;
    uint8_t  context_id    :3; 
    uint8_t  is_resolved  :1;  
    uint8_t  idx_types    :4;       //Mark is index value or other instance
    uint16_t instance_idx;  
    union{
        uint8_t*    static_ui8;
        uint16_t*   static_ui16;
        uint32_t*   static_ui32;
        int8_t*     static_i8;
        int16_t*    static_i16;
        int32_t*    static_i32;
        float*      static_f;
        double*     static_d;
        bool*       static_b; 
    }direct_ptr;    
    idx_u indices[];           //those indices define access way for array, if value
                            //then block will get chosen element, if instance then will search recursive 

} mem_access_arr_t;


/**
 * @brief eaisly switch between array and scalar struct
 */
typedef union {
    mem_access_s_t   *single;
    mem_access_arr_t *arr;
    uint8_t         *raw;
} mem_acces_instance_iter_t;    

//macros to eaisly check idx type
#define IDX_IS_DYNAMIC(node, dim_idx)  (((node)->idx_types >> (dim_idx)) & 0x01)
#define IDX_SET_DYNAMIC(node, dim_idx) ((node)->idx_types |= (1 << (dim_idx)))
#define IDX_SET_STATIC(node, dim_idx)  ((node)->idx_types &= ~(1 << (dim_idx)))


/**
 * @brief Get value by providing mem_access struct
 * @param mem_access_x struct describing what we want
 * @param by_reference Allows  receiveing pointer to destination variable
 * @return emu_variable_t varaible (check its error code for errors)
 */
emu_variable_t mem_get(void * mem_access_x, bool by_reference);


/**
 * @brief Set value by providing mem_access struct
 * @param mem_access_x struct describing what we want
 * @param by_reference Allows  receiveing pointer to destination variable
 * @return emu_result_t EMU_OK when sccess
 */
emu_result_t mem_set(void * mem_access_x, emu_variable_t value);


/**
* @brief Init storage for all references
*/
emu_result_t emu_access_system_init(size_t max_scalar_nodes, size_t array_arena_size_bytes);


void emu_access_system_free(void);


/**
 * @brief Parse one access message (recursive if in need)
 */
emu_err_t mem_access_parse_node_recursive(uint8_t **cursor, size_t *len, void **out_node);



#define GET_VAL(v, member) ((v).by_reference ? *(v).reference.member : (v).data.member)


//Clamp and cast to right type 
#define CLAMP_CAST(VAL, MIN, MAX, TYPE) ({ \
    __typeof__(VAL) _val_tmp = (VAL); \
    /*Turn of compilator cry */ \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wtype-limits\"") \
    TYPE _res = (TYPE)( (_val_tmp) < (MIN) ? (MIN) : ((_val_tmp) > (MAX) ? (MAX) : (_val_tmp)) ); \
    _Pragma("GCC diagnostic pop") \
    _res; \
})

//Converter macro to correct type
#define DEFINE_INT_CONVERTER(T_NAME, T_TYPE, T_MIN, T_MAX) \
__always_inline static inline T_TYPE emu_var_to_##T_NAME(emu_variable_t v) { \
    if (v.by_reference) { \
        switch (v.type) { \
            case DATA_UI8:  return CLAMP_CAST(*v.reference.u8,  T_MIN, T_MAX, T_TYPE); \
            case DATA_UI16: return CLAMP_CAST(*v.reference.u16, T_MIN, T_MAX, T_TYPE); \
            case DATA_UI32: return CLAMP_CAST(*v.reference.u32, T_MIN, T_MAX, T_TYPE); \
            case DATA_I8:   return CLAMP_CAST(*v.reference.i8,  T_MIN, T_MAX, T_TYPE); \
            case DATA_I16:  return CLAMP_CAST(*v.reference.i16, T_MIN, T_MAX, T_TYPE); \
            case DATA_I32:  return CLAMP_CAST(*v.reference.i32, T_MIN, T_MAX, T_TYPE); \
            case DATA_F:    return CLAMP_CAST(*v.reference.f,   T_MIN, T_MAX, T_TYPE); \
            case DATA_D:    return CLAMP_CAST(*v.reference.d,   T_MIN, T_MAX, T_TYPE); \
            case DATA_B:    return (*v.reference.b) ? (T_TYPE)1 : (T_TYPE)0; \
            default: return 0; \
        } \
    } else { \
        switch (v.type) { \
            case DATA_UI8:  return CLAMP_CAST(v.data.u8,  T_MIN, T_MAX, T_TYPE); \
            case DATA_UI16: return CLAMP_CAST(v.data.u16, T_MIN, T_MAX, T_TYPE); \
            case DATA_UI32: return CLAMP_CAST(v.data.u32, T_MIN, T_MAX, T_TYPE); \
            case DATA_I8:   return CLAMP_CAST(v.data.i8,  T_MIN, T_MAX, T_TYPE); \
            case DATA_I16:  return CLAMP_CAST(v.data.i16, T_MIN, T_MAX, T_TYPE); \
            case DATA_I32:  return CLAMP_CAST(v.data.i32, T_MIN, T_MAX, T_TYPE); \
            case DATA_F:    return CLAMP_CAST(v.data.f,   T_MIN, T_MAX, T_TYPE); \
            case DATA_D:    return CLAMP_CAST(v.data.d,   T_MIN, T_MAX, T_TYPE); \
            case DATA_B:    return (v.data.b) ? (T_TYPE)1 : (T_TYPE)0; \
            default: return 0; \
        } \
    } \
}

DEFINE_INT_CONVERTER(u8,  uint8_t,  0,        UINT8_MAX)
DEFINE_INT_CONVERTER(u16, uint16_t, 0,        UINT16_MAX)
DEFINE_INT_CONVERTER(u32, uint32_t, 0,        UINT32_MAX)
DEFINE_INT_CONVERTER(i8,  int8_t,   INT8_MIN, INT8_MAX)
DEFINE_INT_CONVERTER(i16, int16_t,  INT16_MIN, INT16_MAX)
DEFINE_INT_CONVERTER(i32, int32_t,  INT32_MIN, INT32_MAX)

//we need custom for floats and doubles and bools
__always_inline static inline float emu_var_to_f(emu_variable_t v) {
    switch (v.type) {
        case DATA_F:   return GET_VAL(v, f);
        case DATA_D:   return (float)GET_VAL(v, d);
        case DATA_UI8: return (float)GET_VAL(v, u8);
        case DATA_UI16:return (float)GET_VAL(v, u16);
        case DATA_UI32:return (float)GET_VAL(v, u32);
        case DATA_I8:  return (float)GET_VAL(v, i8);
        case DATA_I16: return (float)GET_VAL(v, i16);
        case DATA_I32: return (float)GET_VAL(v, i32);
        case DATA_B:   return GET_VAL(v, b) ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}
//we need custom for floats and doubles and bools
__always_inline static inline double emu_var_to_d(emu_variable_t v) {
    switch (v.type) {
        case DATA_D:   return GET_VAL(v, d);
        case DATA_F:   return (double)GET_VAL(v, f);
        case DATA_UI8: return (double)GET_VAL(v, u8);
        case DATA_UI16:return (double)GET_VAL(v, u16);
        case DATA_UI32:return (double)GET_VAL(v, u32);
        case DATA_I8:  return (double)GET_VAL(v, i8);
        case DATA_I16: return (double)GET_VAL(v, i16);
        case DATA_I32: return (double)GET_VAL(v, i32);
        case DATA_B:   return GET_VAL(v, b) ? 1.0 : 0.0;
        default: return 0.0;
    }
}
//we need custom for floats and doubles and bools
__always_inline static inline bool emu_var_to_b(emu_variable_t v) {
    switch (v.type) {
        case DATA_B:    return GET_VAL(v, b);
        case DATA_UI8:  return GET_VAL(v, u8) != 0;
        case DATA_UI16: return GET_VAL(v, u16) != 0;
        case DATA_UI32: return GET_VAL(v, u32) != 0;
        case DATA_I8:   return GET_VAL(v, i8) != 0;
        case DATA_I16:  return GET_VAL(v, i16) != 0;
        case DATA_I32:  return GET_VAL(v, i32) != 0;
        case DATA_F:    return GET_VAL(v, f) != 0.0f;
        case DATA_D:    return GET_VAL(v, d) != 0.0;
        default: return false;
    }
}
//Mem casts use generic to chose rigt funcion
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

//Get variable then cast and set to provided value (Return error if occured in mem_get)
#define MEM_GET(dst_ptr, mem_access_x) ({ \
    emu_variable_t _tmp_var = mem_get((mem_access_x), false); \
    *(dst_ptr) = MEM_CAST(_tmp_var,(__typeof__(*(dst_ptr)))0); \
    _tmp_var.error; \
})
