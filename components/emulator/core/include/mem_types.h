#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> 

#define TYPES_COUNT 9

typedef enum{
    DATA_UI8  = 0,
    DATA_UI16 = 1,
    DATA_UI32 = 2,
    DATA_I8   = 3,
    DATA_I16  = 4,
    DATA_I32  = 5,
    DATA_F    = 6,
    DATA_D    = 7,
    DATA_B    = 8
}data_types_t;


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
 * @brief This struct is used to mem_set / mem_get so we know type and error 
 */
typedef struct {
    uint8_t   type;
    uint16_t  error;
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


/************************************************************************************************************************* */


//ACCESS TYPES//

/*************************************************************************************************************************** */
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