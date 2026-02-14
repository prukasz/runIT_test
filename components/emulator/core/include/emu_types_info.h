#pragma once
#include "error_types.h"
#include "order_types.h"
#include <stdint.h>

/*********************************************************************************************
 * This header contains strings for error codes and order codes for logging and debugging
 * 
 ********************************************************************************************/

/**
 * @brief total different mem_types_t
 */
#define DATA_TYPES_CNT 7

extern const char* EMU_ERR_TO_STR(emu_err_t err_code);
extern const char* EMU_ORDER_TO_STR(emu_order_t order);
extern const char* MEM_TYPES_TO_STR[DATA_TYPES_CNT];
extern const char* EMU_OWNER_TO_STR(emu_owner_t owner);
extern const char* EMU_LOG_TO_STR(emu_log_t log);


