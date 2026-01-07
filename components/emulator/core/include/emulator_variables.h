#pragma once
#include <stdbool.h>
#include <string.h>
#include "emulator_types.h"
#include "emulator_errors.h"
#include "gatt_buff.h"
#include "math.h"

/**
 * @brief emu_mem_instance_s_t and emu_mem_instance_arr_t represent array or scalar located in memory contexts
 * mem_access_s_t referes to those instances
 **/ 
typedef struct __attribute__((packed)) {
    uint32_t dims_cnt     :4; 
    uint32_t target_type  :4;
    uint32_t updated      :1;  //updated is flag telling block that variable has been updated in cycle
    uint32_t context_id   :3;  //this allows for usage of multiple mem structs
    uint32_t start_idx    :20; //start index in data_pool of chosen type (in elements not bytes!!!)     
} emu_mem_instance_s_t;

typedef struct __attribute__((packed)) {
    uint32_t dims_cnt     :4; 
    uint32_t target_type  :4; 
    uint32_t updated      :1; //updated is flag telling block that variable has been updated in cycle
    uint32_t context_id   :3; //this allows for usage of multiple mem structs
    uint32_t start_idx    :20; //start index in data_pool of chosen type (in elements not bytes!!!)
    uint8_t  dim_sizes[];    
} emu_mem_instance_arr_t;

/**
 * eaisly change s to arr instance
 */
typedef union {
    emu_mem_instance_s_t   *single;
    emu_mem_instance_arr_t *arr;
    uint8_t                *raw;    
} emu_mem_instance_iter_t;


/**
 * @brief This is base memory struct
 */
typedef struct {
    void *data_pools[TYPES_COUNT];//data pools store data of instances of given type 
    void   **instances[TYPES_COUNT]; //store pointers at instances of array and scalars of given type 

    uint16_t next_data_pools_idx[TYPES_COUNT];    //For parse usage, this saves current last index of data used in data pools
    uint16_t next_instance_idx[TYPES_COUNT];      //For parse usage, store last instance idx for given type 
    uint16_t instances_cnt[TYPES_COUNT];          //Store instances count for given type
    void *instances_arenas[TYPES_COUNT];          //here is storage for all instances (we share this place among specific instances)
    size_t instances_arenas_offset[TYPES_COUNT];  //For parse usage, store last used
} emu_mem_t;


/**
 * @brief This struct is used to mem_set / mem_get so we know type and error 
 */
typedef struct {
    uint8_t   type;
    emu_err_t error;
    uint8_t   by_reference : 1;  //we get direct memory pointer
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
    } data;             //we get copy of value
} emu_variable_t;

/**
 * @brief Reset all memory contexts
 */
void emu_mem_free_contexts();


/**
 * @brief get context from id
 */
emu_mem_t* _get_mem_context(uint8_t id); 


/**
 * @brief Create memory context
 * @param context_id under what id
 * @param inst_counts list of total instances for each type (scalars + arrays)
 * @param var_counts total size of data pool to be created in elements 
 * @param total_extra_dims total dimenstions count for each type, used to create arens for instances (cnt += dims_cnt of every array of given type)
 */
emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                            uint16_t var_counts[TYPES_COUNT], 
                            uint16_t inst_counts[TYPES_COUNT], 
                            uint16_t total_extra_dims[TYPES_COUNT]) ;


/**
* @brief Parse for sizes for emu_mem_alloc_context() and create context
*/     
emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source);

/**
* @brief Scan for provided instances definitions
*/  
emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source);

/**
* @brief Scan for provided instances definitions size idx type etc
*/  
emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source);


/**
* @brief This will scan for packets for given context and then fill created variables with provided values
*/                            
emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem);


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