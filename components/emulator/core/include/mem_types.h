#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> 


/*********************************************************************************************************************
 * Types of data used in emulator memory contexts
 * 
 * In emulator we operate using data_types_t enum defining types like UINT8, INT16, FLOAT, DOUBLE, BOOL etc.
 * 
 * In emu_mem_t struct we have data pools for each type storing actual data of instances (arrays and scalars).
 * see emu_mem_t struct for more info.
 * 
 * emu_mem_instance_s_t and emu_mem_instance_arr_t represent scalar and array instances respectively.
 * emu_mem_instance_s_t is base struct for both scalars and arrays, while emu_mem_instance_arr_t extends it with dimension sizes.
 * They emu_mem_instance_iter_t union allows easy switching between scalar and array instance representations.
 * In emulator we treat arrays as scalars with extra dimension info appended:
 * 
 * emu_mem_instance_s_t and emu_mem_instance_arr_t both should be treated as const types with fixed fields that are created only once during parsing.
 * in dim_sizes field of emu_mem_instance_arr_t we have sizes of each dimension of array (up to 4D arrays supported). each max size is 255 elements.
 * 
 * flag "updated" in both instance structs is used to indicate if variable has been updated in current cycle. This applies only to certain contexts like block outputs.
 * 
 * emu_variable_t struct is tagged union used for mem_set and mem_get operations. It contains type info, error code, and data either by reference (direct pointer) or by value (copy).
 * 
 * emu_mem_access_t struct represents access nodes used in block inputs and outputs. It defines how to access a variable, either directly (resolved) or via instance index (recursive).
 * 
 * mem_access_t struct has enum for direct access instance element when all indices are static(during whole code) and known at parse time.
 * If any index is dynamic (changes during execution) then we must use recursive access via instance index.
 * emu_mem_access_s_t and emu_mem_access_arr_t represent scalar and array access types respectively. and has same desing pattern as instance structs for interoperability.
 * In emu_mem_access_arr_t we have array of idx_u unions representing each dimension index, which can be static or refer to another instance. When indices are static we can resolve direct pointer to data during parse time.
 * Is resolved flag indicates if direct pointer is available, allowing for faster access during execution.
 * 
 **********************************************************************************************************************/

#define TYPES_COUNT 9

/**
 * @brief Data types used within emulator
 * @note Please do not change the order of this enum as it is used as index in various arrays and structs
 */
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
 * @brief This is base memory struct for all memory contexts
 */
typedef struct {
    /**
     * @brief Data pools for each data_types_t type stored in separate pools under index defined by data_types_t enum for chosen type
     * Each pool is continuous block of memory allocated during context creation
     * Each pool stores data of instances of given type in elements not bytes
     * For example DATA_UI8 pool stores uint8_t elements, DATA_I16 stores int16_t elements etc
     * when we want to retrieve data we use start_idx from instance struct to get offset in elements
     * for retrieving right data_type we access like data_pools[DATA_UI8] cast to uint8_t* and offset by start_idx
     */
    void *data_pools[TYPES_COUNT];

    /**
    * @brief Instances pointers for each type stored in separate arrays under index defined by data_types_t enum for chosen type
    * Each array stores pointers to instances of given type (scalars and arrays) we store void** to allow for both scalar and array instances in same array
    * when we want to retrieve instance we use instance_idx from mem_access_t struct to get pointer to instance struct
    * to check type of instance we cast emu_instance_iter_t union  and check dims_cnt field in member of union: emu_mem_instance_s_t to avoid memory corruption 
    * (emu_mem_instance_s_t is base struct for both scalar and array instances so size of it is always guaranteed to be correct)
    * Pointers are allocated during context creation and filled during instance parsing contexts
    * Allocation is done using single arena per type to reduce fragmentation and improve cache locality and allocation is made using emu_mem_alloc_context() not direct malloc/calloc
    */
    void   **instances[TYPES_COUNT];

    /**
     * @brief Total count of elemnets in data pool under index defined by data_types_t enum for chosen type
     * This is used to know how many elements are currently stored in each pool for parsing
     */
    uint16_t next_data_pools_idx[TYPES_COUNT];

    /**
     * @brief Total instances count for each type under index defined by data_types_t enum for chosen type
     * So we store last index used during parsing to know where to store next instance pointer
     */
    uint16_t next_instance_idx[TYPES_COUNT];   
    
    /**
     * @brief Total instances count for each type under index defined by data_types_t enum for chosen type
    */
    uint16_t instances_cnt[TYPES_COUNT];

    /**
     * @brief Here are stored arenas for instances allocation for each type 
     * We use single arena per type to reduce fragmentation and improve cache locality
     * Allocation is done during context creation using emu_mem_alloc_context() function
     * Each arena is continuous block of memory used to allocate instance structs (scalars and arrays) for given type
     */
    void *instances_arenas[TYPES_COUNT];

    /**
     * @brief Offset in instances_arenas for each type to know where to allocate next instance struct
     * This is used during parsing to know where to store next instance struct in arena
     */
    size_t instances_arenas_offset[TYPES_COUNT];
} emu_mem_t;



/**
 * @brief emu_mem_instance_s_t and emu_mem_instance_arr_t represent array or scalar located in memory contexts
 * mem_access_s_t referes to those instances
 **/ 
typedef struct{
    void* data;
    uint16_t dims_cnt     :4; 
    uint16_t target_type  :4;
    uint16_t updated      :1; 
    uint16_t context_id   :3;  
    uint16_t _reserved    :4;    
} emu_mem_instance_s_t;


typedef struct{
    void* data;
    uint16_t dims_cnt     :4; 
    uint16_t target_type  :4;
    uint16_t updated      :1;
    uint16_t context_id   :3; 
    uint16_t _reserved    :4;  
    uint8_t  dim_sizes[];     
} emu_mem_instance_arr_t;

/**
 * @brief This union allows easy switching between scalar and array instance representations
 * allow to treat both types uniformly when needed as base struct is always emu_mem_instance_s_t)
 * raw pointer allows byte-level access or just pointer assinment 
 */
typedef union {
    emu_mem_instance_s_t   *single;
    emu_mem_instance_arr_t *arr;
    uint8_t                *raw;    
} emu_mem_instance_iter_t;

/**
 * @brief This tagged union is used for mem_set and mem_get operations or used in blocks to store variable type and value 
 */
typedef struct {
    /**
     * @brief Data type of the variable (from data_types_t enum)
     */
    uint8_t   type;  
    /**
     * @brief Error code (emu_err_t) from last operation (EMU_OK if no error)
     */
    uint16_t  error;
    /**
     * @brief Flag indicating if data is stored by reference (direct pointer) or by value (copy)
     * By reference means that reference union contains direct pointer to data and then we can set/get value directly
     */
    uint8_t   by_reference : 1;  
    uint8_t   _reserved    : 7;
    /**
     * @brief Data of the variable by reference (direct pointer) with types defined in data_types_t enum
     */
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
    /**
     * @brief Data of the variable by value (copy) with types defined in data_types_t enum
     */
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


/*************************************************************************************************************************
Here are defined mem_access_s_t and mem_access_arr_t structs used in block inputs and outputs to define how to access variables
They can be used elsewhere where we need to access variables in memory contexts especially those from blocks outputs and globlal variables

*************************************************************************************************************************** */

/**
 * @brief This index union represent one index value of array. 
 * It can be static value or can refer to other instance as value and we will need to resolve it during execution
 */
typedef union {
        uint16_t static_idx;
        void* next_instance; 
} idx_u;

/**
 * @brief This struct is used for acces to scalar instances in memory contexts
 */
typedef struct{
        void *instance;
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
        uint8_t  is_resolved  :1;  
        uint8_t  reserved     :7; 
} mem_access_s_t;


/**
 * @brief This struct is used for acces to array instances in memory contexts
 * see mem_access_s_t for more info
 */
typedef struct { 
    void *instance;
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
        uint8_t  is_resolved   :1;  
        uint8_t  is_idx_static :7;    
    idx_u indices[];        
} mem_access_arr_t;


/**
 * @brief eaisly switch between array and scalar struct
 */
typedef union {
    mem_access_s_t   *single;
    mem_access_arr_t *arr;
    uint8_t         *raw;
} mem_acces_instance_iter_t;    