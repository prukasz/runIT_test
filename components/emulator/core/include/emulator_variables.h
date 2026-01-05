    #pragma once
    #include <stdbool.h>
    #include <string.h>
    #include "emulator_types.h"
    #include "emulator_errors.h"
    #include "gatt_buff.h"
    #include "math.h"

     
    typedef struct __attribute__((packed)) {
        uint32_t dims_cnt     :4; 
        uint32_t target_type  :4;
        uint32_t updated      :1;
        uint32_t reference_id :3;  //this allows for usage of multiple mem structs
        uint32_t start_idx    :20;      
    } emu_mem_instance_s_t;

    typedef struct __attribute__((packed)) {
        uint32_t dims_cnt     :4; 
        uint32_t target_type  :4;
        uint32_t updated      :1;
        uint32_t reference_id :3;  //this allows for usage of multiple mem structs
        uint32_t start_idx    :20;
        uint8_t  dim_sizes[];    
    } emu_mem_instance_arr_t;

    typedef union {
        emu_mem_instance_s_t   *single;
        emu_mem_instance_arr_t *arr;
        uint8_t                *raw;    
    } emu_mem_instance_iter_t;

    typedef struct {
        void *data_pools[TYPES_COUNT];
        void   **instances[TYPES_COUNT];
        uint8_t  sizes[TYPES_COUNT];

        uint16_t next_data_idx[TYPES_COUNT]; 
        uint16_t next_instance_idx[TYPES_COUNT];
        uint16_t instances_cnt[TYPES_COUNT];
        void *arenas[TYPES_COUNT];
        size_t arena_offset[TYPES_COUNT];
    } emu_mem_t;


    typedef struct {
        uint8_t   type;
        emu_err_t error;
        uint8_t   by_reference : 1; 
        uint8_t   _reserved    : 7;
        union {
            uint8_t  *u8;
            uint16_t *u16;
            uint32_t *u32;
            int8_t   *i8;
            int16_t  *i16;
            int32_t  *i32;
            float    *f;
            double   *d;
            bool     *b;
            void     *raw;
        } reference;
        union {
            uint8_t  u8;
            uint16_t u16;
            uint32_t u32;
            int8_t   i8;
            int16_t  i16;
            int32_t  i32;
            float    f;
            double   d;
            bool     b;
        } data;
    } emu_variable_t;


    void emu_mem_free_contexts();
    emu_mem_t* _get_mem_context(uint8_t id);

    emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                               uint16_t var_counts[TYPES_COUNT], 
                               uint16_t inst_counts[TYPES_COUNT], 
                               uint16_t total_extra_dims[TYPES_COUNT]) ;


    emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem);

    emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source);

    emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source);

    emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source);


    static const size_t DATA_TYPE_SIZES[TYPES_COUNT] = {
        sizeof(uint8_t),  // UI8
        sizeof(uint16_t), // UI16
        sizeof(uint32_t), // UI32
        sizeof(int8_t),   // I8
        sizeof(int16_t),  // I16
        sizeof(int32_t),  // I32
        sizeof(float),    // F
        sizeof(double),   // D
        sizeof(bool)      // B
    };