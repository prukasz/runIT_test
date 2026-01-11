#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "error_types.h"
#include "mem_types.h"
#include "loop_types.h"
#include "block_types.h"

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)


#define TYPES_COUNT 9 

/**
* @brief Data types used within emulator
*/





/**
* @brief Orders for emulator interface to execute
*/



/**
* @brief Block packet header
* @attention Each packet must start with emu_block_header_t or be single emu_order_t
*/
typedef enum{
    EMU_H_BLOCK_CNT       = 0xB000,
    EMU_H_BLOCK_START     = 0xBB,

    EMU_H_BLOCK_PACKET_IN_START  = 0xF0,
    EMU_H_BLOCK_PACKET_OUT_START = 0xE0,
    EMU_H_BLOCK_PACKET_CFG       = 0x00,
    EMU_H_BLOCK_PACKET_CONST     = 0x01,
    EMU_H_BLOCK_PACKET_CUSTOM    = 0x02,
}emu_block_header_t;

typedef enum{
    //[HEADER][ctx_id][9x][UINT16_T TOTAL_CNT_OF_SLOTS] [9x] [UINT16_T TOTAL_CNT_OF_INSTANCE],[9x] [UINT16_T TOTAL_CNT_OF_EXTRA_SPACE] 
    VAR_H_SIZES = 0xFF00,
    //[HEADER][ctx_id][9x][UINT16_T scalar_cnt](in enum order)
    VAR_H_SCALAR_CNT = 0xFF01,
    //[HEADER][ctx_id]N*([uint8_t dimscnt][uint8_t type][uint8_t idx])
    VAR_H_ARR = 0xFF02,

    /*******************ARRAYS AND SCALARS DATA**************************************** */
    //[HEADER][ctx_id], [ui16_t idx] [data], [ui16_t idx] [data], [ui16_t idx] [data] .. 
    VAR_H_DATA_S_UI8  = 0x0F10,
    VAR_H_DATA_S_UI16 = 0x0F20,
    VAR_H_DATA_S_UI32 = 0x0F30,
    VAR_H_DATA_S_I8   = 0x0F40,
    VAR_H_DATA_S_I16  = 0x0F50,
    VAR_H_DATA_S_I32  = 0x0F60,
    VAR_H_DATA_S_F    = 0x0F70,
    VAR_H_DATA_S_D    = 0x0F80,
    VAR_H_DATA_S_B    = 0x0F90,
    //[HEADER][ctx_id], [ui16_t idx,][ui16_t in_arr_idx_offset][data](till end of packet)
    VAR_H_DATA_ARR_UI8  = 0xFFF0,
    VAR_H_DATA_ARR_UI16 = 0xFFF1,
    VAR_H_DATA_ARR_UI32 = 0xFFF2,
    VAR_H_DATA_ARR_I8   = 0xFFF3,
    VAR_H_DATA_ARR_I16  = 0xFFF4,
    VAR_H_DATA_ARR_I32  = 0xFFF5,
    VAR_H_DATA_ARR_F    = 0xFFF6,
    VAR_H_DATA_ARR_D    = 0xFFF7,
    VAR_H_DATA_ARR_B    = 0xFFF8,
}emu_variables_headers_t;



/**
* @brief Block type identification code
*/
typedef enum{
    BLOCK_MATH = 0x01,
    BLOCK_SET = 0x02,
    BLOCK_LOGIC = 0x03,
    BLOCK_COUNTER = 0x04,
    BLOCK_CLOCK = 0x05,
    BLOCK_FOR = 0x08,
    BLOCK_TIMER = 0x09,
}block_type_t;




