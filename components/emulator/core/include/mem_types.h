#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> 



#define TYPES_COUNT 9

/**
 * @brief Data types used within emulator
 * @note Please do not change the order of this enum as it is used as index in various arrays and structs
 */
typedef enum{
    DATA_U8  = 0,
    DATA_U16 = 1,
    DATA_U32 = 2,
    DATA_I8   = 3,
    DATA_I16  = 4,
    DATA_I32  = 5,
    DATA_F    = 6,
    DATA_D    = 7,
    DATA_B    = 8
}data_types_t;

/**
*@brief Allow to retive ptr of given type
*/
typedef union{
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
 }mem_ptr_u;

/**
*@brief Allow to retive value of given type
*/
typedef union{
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int8_t   i8;
    int16_t  i16;
    int32_t  i32;
    float    f;
    double   d;
    bool     b;
 }mem_val_u;

/**
*@brief Structure describing any instance
*/
typedef struct __attribute__((packed)){
    mem_ptr_u data;            //4 
    uint16_t context   : 3; 
    uint16_t dims_cnt  : 4;
    uint16_t type      : 4;
    uint16_t updated   : 1;
    uint16_t can_clear : 1;
    uint16_t reserved  : 3;   
    uint16_t dims_idx;         //index in table of dims 
}mem_instance_t; 

typedef struct mem_access_t mem_access_t;

/**
*@brief Provided index value, static or dynamic
*/
typedef union{
    uint32_t       static_index;
    mem_access_t* dynamic_index;
}idx_val_t;

/**
*@brief Allow to retive value form any instance
*/
typedef struct mem_access_t {
    mem_instance_t* instance;  //4

    uint16_t resolved_offset;  //6

    uint8_t  idx_cnt     : 4;
    uint8_t  is_resolved : 1;
    uint8_t  can_resolve : 1;
    uint8_t  reserved    : 2;
    uint8_t  is_idx_static;//8
    idx_val_t indices[];  //+4
} mem_access_t;

/**
*@brief Allow to keep type known runtime
*/
typedef struct{
    union{
        mem_ptr_u ptr;
        mem_val_u val;
    }data;
    uint32_t type     : 2; 
    uint32_t by_reference:1;
    uint32_t reserved : 29;
}mem_var_t;

typedef struct{
    mem_instance_t* instances;   /*array of instances*/
    uint16_t        inst_cnt;    /*total instances count*/
    uint16_t        inst_cursor; /*next avaible instance index*/

    uint16_t* dims_pool;   /*pool for dims to don't hold them in instances*/
    uint32_t  dims_cursor; /*next avaible index in array of dimms*/
    uint32_t  dims_cap;
    
    mem_ptr_u  data_heap; /*base address of data heap */
    uint32_t data_cursor;
    uint32_t data_heap_size;
}type_manager_t;


typedef struct{
    /**
    *@brief separate struct for every type
    */
    type_manager_t types[TYPES_COUNT];
}mem_context_t;



 
