 #pragma once
#include "mem_types.h"

/** 
 * @brief block structure each block shall depend on data accesed from this struct members
 */
typedef struct{
    mem_access_t **inputs; /*Instances to use*/
    mem_access_t **outputs; /*Instances to store result*/
    void *custom_data; /*block specific data*/
    __packed struct {
        uint16_t block_idx; /*index of block in code*/
        uint16_t in_connceted_mask; /*connected inputs (those that have instance)*/
        uint8_t block_type; /*typeof block (function)*/
        uint8_t in_cnt; /*count of input */
        uint8_t q_cnt; /*count of outputs */
    }cfg;
}block_data_t;

/*block hanlde*/
typedef block_data_t* block_handle_t;


