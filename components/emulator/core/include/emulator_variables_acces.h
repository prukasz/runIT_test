#pragma once 
#include "stdint.h"
#include "stdbool.h"
#include "emulator_errors.h"
#include "emulator_variables.h"
#include "emulator_blocks.h"

typedef union {
        uint16_t static_idx;
        void* next_node; 
} idx_u;

typedef struct __attribute__((packed)) {
        uint8_t  dims_cnt     :4; 
        uint8_t  target_type  :4;
        uint8_t  reference_id :4;  //this allows for usage of multiple mem structs
        uint8_t _reserved     :4;   
        uint16_t target_idx;   
} mem_acces_s_t;

typedef struct __attribute__((packed)) { 
    uint8_t  dims_cnt     :4; 
    uint8_t  target_type  :4;
    uint8_t  reference_id :4;  //this allows for usage of multiple mem structs
    uint8_t  idx_types    :4;
    uint16_t target_idx;     
    idx_u    indices[];       
} mem_acces_arr_t;

typedef union {
    mem_acces_s_t   *single;
    mem_acces_arr_t *arr;
    uint8_t         *raw;
} mem_acces_instance_iter_t;

#define IDX_IS_DYNAMIC(node, dim_idx)  (((node)->idx_types >> (dim_idx)) & 0x01)
#define IDX_IS_STATIC(node, dim_idx)   (!IDX_IS_DYNAMIC(node, dim_idx))
#define IDX_SET_DYNAMIC(node, dim_idx) ((node)->idx_types |= (1 << (dim_idx)))
#define IDX_SET_STATIC(node, dim_idx)  ((node)->idx_types &= ~(1 << (dim_idx)))

#define CLAMP_CAST(VAL, MIN, MAX, TYPE) \
    (TYPE)( (VAL) < (MIN) ? (MIN) : ((VAL) > (MAX) ? (MAX) : (VAL)) )


/**
 * @brief Get value by providing mem_acces struct
 * @param global_access_x struct describing what we want
 * @param by_reference Allows  receiveing pointer to destination variable
 * @return value
 */
emu_variable_t mem_get(void * global_access_x, bool by_reference);
emu_err_t mem_set(void * global_access_x, emu_variable_t value);
emu_mem_instance_iter_t mem_get_instance(void *access_node);
emu_mem_instance_iter_t _mem_get_instance(emu_mem_t *mem, uint8_t type, uint16_t idx);

void emu_refs_system_init(size_t max_scalar_nodes, size_t arena_size_bytes);
void emu_refs_system_free(void);
emu_result_t emu_parse_block_inputs(chr_msg_buffer_t *source, block_handle_t *block);
emu_result_t emu_parse_block_outputs(chr_msg_buffer_t *source, block_handle_t *block);

static inline double emu_var_to_double(emu_variable_t v) {
    if (v.by_reference) {
        switch (v.type) {
            case DATA_UI8:  return (double)(*v.reference.u8);
            case DATA_UI16: return (double)(*v.reference.u16);
            case DATA_UI32: return (double)(*v.reference.u32);
            case DATA_I8:   return (double)(*v.reference.i8);
            case DATA_I16:  return (double)(*v.reference.i16);
            case DATA_I32:  return (double)(*v.reference.i32);
            case DATA_F:    return (double)(*v.reference.f);
            case DATA_D:    return (*v.reference.d);
            case DATA_B:    return (*v.reference.b) ? 1.0 : 0.0;
            default: return 0.0;
        }
    } else {
        switch (v.type) {
            case DATA_UI8:  return (double)v.data.u8;
            case DATA_UI16: return (double)v.data.u16;
            case DATA_UI32: return (double)v.data.u32;
            case DATA_I8:   return (double)v.data.i8;
            case DATA_I16:  return (double)v.data.i16;
            case DATA_I32:  return (double)v.data.i32;
            case DATA_F:    return (double)v.data.f;
            case DATA_D:    return v.data.d;
            case DATA_B:    return v.data.b ? 1.0 : 0.0;
            default: return 0.0;
        }
    }
}