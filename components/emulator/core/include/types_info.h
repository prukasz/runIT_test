#pragma once
#include "error_types.h"
#include "order_types.h"
#include <stdint.h>

/*********************************************************************************************
 * This header contains strings for error codes and order codes for logging and debugging
 * 
 ********************************************************************************************/

/**
 * @brief total different data_types_t
 */
#define DATA_TYPES_CNT 9

extern const uint8_t DATA_TYPE_SIZES[DATA_TYPES_CNT];
extern const char* EMU_ERR_TO_STR(emu_err_t err_code);
extern const char* EMU_ORDER_TO_STR(emu_order_t order);
extern const char* EMU_DATATYPE_TO_STR[DATA_TYPES_CNT];

