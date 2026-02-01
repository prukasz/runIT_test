#pragma once
#include <stdint.h>
#include <stdbool.h> 


#define MEM_TYPES_COUNT 7

/**
 * @brief Data types used within contexts
 * @note Please do not change the order of this enum as it is used as index in various arrays and structs
 */
typedef enum{
    MEM_U8   = 0,
    MEM_U16  = 1,
    MEM_U32  = 2,
    MEM_I16  = 3,
    MEM_I32  = 4,
    MEM_B    = 5,
    MEM_F    = 6
}mem_types_t;

static const uint8_t MEM_TYPE_SIZES[MEM_TYPES_COUNT] = {
    1, // U8
    2, // U16
    4, // U32
    2, // I16
    4, // I32
    1, // B
    4  // F
};

/** 
*@brief Allow to retive ptr of given type
*/
typedef union{
    uint8_t  *u8;
    uint16_t *u16;
    uint32_t *u32;
    int16_t  *i16;
    int32_t  *i32;
    float    *f;
    bool     *b;
    void     *raw;
 }mem_types_ptr_u;

/**
*@brief Allow to retive value of given type
*/
typedef union{
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int16_t  i16;
    int32_t  i32;
    float    f;
    bool     b;
 }mem_types_val_u;

/**
*@brief Structure describing any instance
*/
typedef struct __packed {
    mem_types_ptr_u data;    /*Data storage pointer*/          
    uint16_t context   : 3;  /*Context that isnstance shall belong to*/
    uint16_t type      : 4;  /*mem_types_t type*/
    uint16_t dims_cnt  : 4;  /*dimensions count in case of arrays > 0*/
    uint16_t updated   : 1;  /*Updated flag can be used for block output variables*/
    uint16_t can_clear : 1;  /*Can updated flag be cleared*/
    uint16_t reserved  : 3;  /*padding*/
    uint16_t dims_idx;       /*Index in table of dimensions this table is stored in context for selected type*/ 
}mem_instance_t; 

typedef struct mem_access_t mem_access_t; //forward declaration
/**
*@brief Provided index value, static or dynamic
*/
typedef union{
    uint16_t       static_index; /*@note we have capacity for uint32_t but uint16_t is used everywhere*/
    mem_access_t* dynamic_index; /*@note mem_get will be used to get index val*/
}idx_val_t;

/**
*@brief Allow to retrive value form instance 
*@note Support nesting mem_access_t as index value
*/
typedef struct mem_access_t {
    mem_instance_t* instance;  /*Ptr at instance*/
    uint16_t resolved_index;   /*Resolved index for array (if possible)*/
    uint8_t  indices_cnt       : 4; /*Provided indices cnt*/
    uint8_t  is_index_resolved : 1; /*Is array index resolved*/
    uint8_t  can_resolve_index : 1; /*Can array index be resolved*/
    uint8_t  reserved          : 2; /*paddign*/
    uint8_t  is_idx_static_mask; /*mask for provided array indices type example: 0x01 means that idx 0 is number and rest is mem_access_t*/
    idx_val_t indices_values[]; /*Indices values as number or mem_access_t*/
} mem_access_t;

/**
*@brief Tagged union to know type runtime
*@note consider packing ?
*/
typedef struct {
    union{
        mem_types_ptr_u ptr; /*as pointer at instance data*/
        mem_types_val_u val; /*as copy of value from instace data*/
    }data; /*select based on by_reference mask*/
    uint32_t type     : 4;  /*mem_types_t type equal to instance type */
    uint32_t by_reference:1; /*Is data given as ptr or copy*/
    uint32_t reserved : 27; /*padding*/
}mem_var_t;

typedef struct{
    mem_instance_t* instances;   /*array of instances*/
    uint16_t        instances_cap;    /*total instances count*/
    uint16_t        instances_cursor; /*next avaible instance index*/

    uint16_t* dims_pool;   /*pool for dims to don't hold them in instances*/
    uint32_t  dims_cursor; /*next avaible index in array of dimms*/
    uint32_t  dims_cap;  /*max extra dims*/
    
    mem_types_ptr_u  data_heap; /*base address of data heap */
    uint32_t data_heap_cursor;   /*next data index that can be used*/
    uint32_t data_heap_cap; /*in mem_types_t items*/
}type_manager_t;

/**
 * @brief Context structure
 */
typedef struct{
    type_manager_t types[MEM_TYPES_COUNT]; /*separate struct for every type*/
}mem_context_t;
