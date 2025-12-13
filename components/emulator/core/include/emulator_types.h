#pragma once

/******************************************
Specific enums used in emulator code
******************************************/

/**
 * @brief block type identifier (what block does)
 */
typedef enum {
    BLOCK_PASS_IN_OUT = 0x00,
    BLOCK_PASS_GLOBAL = 0xFA,
}block_special_packet_t;

/**
* @brief Data types used within emulator
*/
typedef enum{
    //uint8_t
    DATA_UI8  = 0,
    //uint16_t
    DATA_UI16 = 1,
    //uint32_t
    DATA_UI32 = 2,
    //int8_t
    DATA_I8   = 3,
    //int16_t
    DATA_I16  = 4,
    //int32_t
    DATA_I32  = 5,
    //float
    DATA_F    = 6,
    //double
    DATA_D    = 7,
    //bool
    DATA_B    = 8
}data_types_t;

extern const char *DATA_TYPE_TO_STR[9];

/**
* @brief Orders for emulator interface to execute
*/
typedef enum {
    ORD_STOP_BYTES        = 0x0000,    
    ORD_START_BYTES       = 0xFFFF,
    ORD_PARSE_INTO_VARIABLES    = 0xDDDD,
    ORD_START_BLOCKS      = 0x00FF,
    ORD_H_VARIABLES       = 0xFF00,
    ORD_PARSE_VARIABLES = 0xEEEE,
    ORD_RESET_ALL         = 0x0001, 
    ORD_RESET_BLOCKS      = 0x0002,
    ORD_RESET_MGS_BUF     = 0x0003, 
    ORD_PROCESS_CODE      = 0x0020,
    ORD_CHECK_CODE        = 0x0030,
    ORD_EMU_LOOP_START      = 0x1000,
    ORD_EMU_LOOP_STOP     = 0x2000,
    ORD_EMU_LOOP_INIT     = 0x2137,
    ORD_EMU_ALLOCATE_BLOCKS_LIST = 0xb100,
    ORD_EMU_FILL_BLOCKS_LIST = 0xb200,
}emu_order_t;


/**
* @brief Block packet header
* @attention Each packet must start with emu_header_t or be single emu_order_t
*/
typedef enum{
    EMU_H_VAR_ORD    = 0xFFFF,
    EMU_H_VAR_START  = 0xFF00,
    EMU_H_VAR_ARR_1D = 0xFF01,
    EMU_H_VAR_ARR_2D = 0xFF02,
    EMU_H_VAR_ARR_3D = 0xFF03,
    EMU_H_VAR_ARR_UI8 = 0xFF10,
    EMU_H_VAR_ARR_UI16 = 0xFF20,
    EMU_H_VAR_ARR_UI32 = 0xFF30,
    EMU_H_VAR_ARR_I8 = 0xFF40,
    EMU_H_VAR_ARR_I16 = 0xFF50,
    EMU_H_VAR_ARR_I32 = 0xFF60,
    EMU_H_VAR_ARR_F = 0xFF70,
    EMU_H_VAR_ARR_D = 0xFF80,
    EMU_H_VAR_ARR_B = 0xFF90,
    EMU_H_VAR_SCAL_UI8 = 0xFF11,    //single varialbes
    EMU_H_VAR_SCAL_UI16 = 0xFF21,
    EMU_H_VAR_SCAL_UI32 = 0xFF31,
    EMU_H_VAR_SCAL_I8 = 0xFF41,
    EMU_H_VAR_SCAL_I16 = 0xFF51,
    EMU_H_VAR_SCAL_I32 = 0xFF61,
    EMU_H_VAR_SCAL_F = 0xFF71,
    EMU_H_VAR_SCAL_D = 0xFF81,
    EMU_H_VAR_SCAL_B = 0xFF91,
    EMU_H_BLOCK_CNT   = 0xB000,
    EMU_H_BLOCK_PACKET= 0xBB,
    EMU_H_BLOCK_START_G_ACCES = 0xF0,
}emu_header_t;

/**
* @brief emulator main loop flags
*/
typedef enum {
    LOOP_NOT_SET,
    LOOP_SET,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

/**
* @brief Block type identification code
*/
typedef enum{
    BLOCK_COMPUTE = 0x01,
    BLOCK_INJECT  = 0xFF
}block_type_t;

