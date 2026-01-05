#pragma once
#include <stdint.h>
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)


#define TYPES_COUNT 9 

/**
* @brief Data types used within emulator
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

static const uint8_t TYPE_SIZES[TYPES_COUNT] = {
    1, // DATA_UI8
    2, // DATA_UI16
    4, // DATA_UI32
    1, // DATA_I8
    2, // DATA_I16
    4, // DATA_I32
    4, // DATA_F
    8, // DATA_D
    1  // DATA_B
    };

static const char *DATA_TYPE_TO_STR[9] = {
    "DATA_UI8",
    "DATA_UI16",
    "DATA_UI32",
    "DATA_I8",
    "DATA_I16",
    "DATA_I32",
    "DATA_F",
    "DATA_D",
    "DATA_B"
};

/**
* @brief Orders for emulator interface to execute
*/
typedef enum {
    /********PARSER ORDERS **************/
    ORD_CREATE_CONTEXT        = 0xFFFF,  //Create context with provided size
    ORD_PARSE_VARIABLES       = 0xEEEE,  //Parse variables types and arrays sizes
    ORD_PARSE_VARIABLES_DATA  = 0xDDDD,  //Fill created variables with provided data
    ORD_EMU_CREATE_BLOCK_LIST = 0xb100,  //Create list for number of provided blocks (Total blocks in code)
    ORD_EMU_CREATE_BLOCKS     = 0xb200,  //Create blocks (Inputs, Outputs, Type, Custom data)
    ORD_CHECK_CODE            = 0x0030,  //Check code completeness before start (once after parsing finish)

    /********RESET ORDERS  ***************/
    ORD_RESET_ALL             = 0x0001,  //Brings emulator to startup state, provides way to eaisly send new code
    ORD_RESET_BLOCKS          = 0x0002,  //Reset all blocks and theirs data
    ORD_RESET_MGS_BUF         = 0x0003,  //Clear msg buffer
    
    /********LOOP CONTROL****************/
    ORD_EMU_LOOP_START     = 0x1000, //start loop / resume 
    ORD_EMU_LOOP_STOP      = 0x2000, //stop loop aka pause
    ORD_EMU_LOOP_INIT      = 0x3000, //init loop first time 

    /********DEBUG OPTIONS **************/
    ORD_EMU_INIT_WITH_DBG  = 0x3333, //init loop with debug
    ORD_EMU_SET_PERIOD     = 0x4000, //change period of loop
    ORD_EMU_RUN_ONCE       = 0x5000, //Run one cycle and wait for next order
    ORD_EMU_RUN_WITH_DEBUG = 0x6000, //Run with debug (Dump after each cycle)
    ORD_EMU_RUN_ONE_STEP   = 0x7000, //Run one block / one step (With debug)

}emu_order_t;


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
* @brief emulator main loop flags
*/
typedef enum {
    LOOP_NOT_SET,
    LOOP_CREATED,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

/**
* @brief Block type identification code
*/
typedef enum{
    BLOCK_MATH = 0x01,
    BLOCK_SET = 0x02,
    BLOCK_LOGIC = 0x03,
    BLOCK_FOR = 0x08,
    BLOCK_TIMER = 0x09,
}block_type_t;

