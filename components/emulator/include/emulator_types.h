#pragma once

/**
* @brief emulator scope errors
*/
typedef enum {
    EMU_OK = 0,
    EMU_ERR_INVALID_STATE,
    EMU_ERR_INVALID_ARG,
    EMU_ERR_NO_MEMORY,
    EMU_ERR_CMD_START,
    EMU_ERR_CMD_STOP,
    EMU_ERR_CMD_START_BLOCKS,
    EMU_ERR_INVALID_DATA,
    EMU_ERR_UNLIKELY
}emu_err_t;

/**
* @brief Data types used within the emulator.
*/
typedef enum{
    DATA_UI8,
    DATA_UI16,
    DATA_UI32,
    DATA_I8,
    DATA_I16,
    DATA_I32,
    DATA_F,
    DATA_D,
    DATA_B
}data_types_t;


/**
* @brief Orders to execute
*/
typedef enum {
    ORD_STOP_BYTES        = 0x0000,
    ORD_START_BYTES       = 0xFFFF,
    ORD_FILL_VARIABLES    = 0xDDDD,
    ORD_START_BLOCKS      = 0x00FF,
    ORD_H_VARIABLES       = 0xFF00,
    ORD_PROCESS_VARIABLES = 0xEEEE,
    ORD_RESET_TRANSMISSION= 0x0001, 
    ORD_PROCESS_CODE      = 0x0020,
    ORD_CHECK_CODE        = 0x0030,
    ORD_EMU_LOOP_RUN      = 0x1000,
    ORD_EMU_LOOP_STOP     = 0x2000,
    ORD_EMU_LOOP_INIT     = 0x2137,
}emu_order_t;


/**
* @brief Block type identifiers.
*/

typedef enum{
    EMU_H_VAR_ORD    = 0xFFFF,
    EMU_H_VAR_START  = 0xFF00,
    EMU_H_VAR_ARR_1D = 0xFF01,
    EMU_H_VAR_ARR_2D = 0xFF02,
    EMU_H_VAR_ARR_3D = 0xFF03,
    EMU_H_VAR_DATA_0 = 0xFF10,
    EMU_H_VAR_DATA_1 = 0xFF20,
    EMU_H_VAR_DATA_2 = 0xFF30,
    EMU_H_VAR_DATA_3 = 0xFF40,
    EMU_H_VAR_DATA_4 = 0xFF50,
    EMU_H_VAR_DATA_5 = 0xFF60,
    EMU_H_VAR_DATA_6 = 0xFF70,
    EMU_H_VAR_DATA_7 = 0xFF80,
    EMU_H_VAR_DATA_8 = 0xFF90,
    EMU_H_VAR_DATA_S0 = 0xFF11,    //single varialbes
    EMU_H_VAR_DATA_S1 = 0xFF21,
    EMU_H_VAR_DATA_S2 = 0xFF31,
    EMU_H_VAR_DATA_S3 = 0xFF41,
    EMU_H_VAR_DATA_S4 = 0xFF51,
    EMU_H_VAR_DATA_S5 = 0xFF61,
    EMU_H_VAR_DATA_S6 = 0xFF71,
    EMU_H_VAR_DATA_S7 = 0xFF81,
    EMU_H_VAR_DATA_S8 = 0xFF91,
}emu_header_t;

typedef enum {
    LOOP_NOT_SET,
    LOOP_SET,
    LOOP_RUNNING,
    LOOP_STOPPED,
    LOOP_HALTED,
    LOOP_FINISHED,
} loop_status_t;

typedef enum{
    ID_1 = 0x00,
}block_id_t;