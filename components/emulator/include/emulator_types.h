#pragma once
#include "stdint.h"
#include "stddef.h"
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
}emu_err_t;

/**
* @brief Data types used within the emulator.
*/
typedef enum{
    DATA_UI8,
    DATA_UI16,
    DATA_UI32,
    DATA_UI64,
    DATA_I8,
    DATA_I16,
    DATA_I32,
    DATA_I64,
    DATA_F,
    DATA_D,
    DATA_B
}data_types_t;

/**
* @brief Block type identifiers.
*/
typedef struct{
uint8_t id;
}block_id_t;