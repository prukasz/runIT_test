#pragma once
/**
 * @file emulator_errors.h
 * @brief Compatibility/convenience header for blocks error handling.
 * 
 * Provides access to the canonical error macros (EMU_RETURN_CRITICAL, 
 * EMU_RETURN_WARN, EMU_RETURN_NOTICE, EMU_RETURN_OK, EMU_REPORT) and 
 * defines EMU_RESULT_OK() for simple success returns.
 */

#include "error_types.h"
#include "error_macros.h"
#include "types_info.h"

/**
 * @brief Convenience macro to return a simple OK result without logging.
 * Use this when you just need to return success without pushing a report.
 */
#define EMU_RESULT_OK() ((emu_result_t){ .code = EMU_OK })
