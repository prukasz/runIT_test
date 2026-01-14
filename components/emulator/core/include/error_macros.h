#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "error_types.h"
#include "emulator_loop.h"
#include "emulator_logs_config.h"

// --- Global Queue Handles ---
extern QueueHandle_t error_logs_queue_t;
extern QueueHandle_t logs_queue_t;
/**
 * @brief This header contains macros for error handling and logging within the emulator.
 * It provides standardized ways to report errors, warnings, and notices,
 * as well as logging messages with context awareness (ISR safe).
 * It utilizes FreeRTOS queues to push error reports and logs for asynchronous processing.
 * 
 * There are 2 queues:
 * - error_logs_queue_t: for error reports (emu_result_t)
 * - logs_queue_t: for general logs and reports (emu_log_t - on status change mainly)
 * 
 * ERROR MACROS:
 * - EMU_RETURN_CRITICAL: for critical errors that should stop execution
 * - EMU_RETURN_WARN: for warnings that allow execution to continue
 * - EMU_RETURN_NOTICE: for informational notices
 * - EMU_REPORT_CRITICAL: for reporting errors without returning
 * - EMU_REPORT_WARN: for reporting warnings without returning
 * - EMU_REPORT_NOTICE: for reporting notices without returning
 * LOGGING MACROS:
 * - LOG_I, LOG_W, LOG_E, LOG_D: for logging info, warnings, errors, and debug messages directly to ESP log
 * - These macros are ISR safe and check context before logging.
 * - EMU_RETURN_OK: for returning success results we add to logs_queue_t not error queue as its not an error
 * - EMU_REPORT: for reporting success results we add to logs_queue_t not error queue as its not an error
 * 
 * For more details on each macro, see their definitions below.
 * For error code definitions, see error_types.h
 * For log configuration, see emulator_logs_config.h
 * For emu_result_t structure, see error_types.h
 * 
 * PLEASE NOTE:
 * Use constant Enum names for owner_name_enum parameters to ensure consistency in error reporting.
 * Use constant TAG strings for logging to maintain clarity in log outputs.
 * Use constant owner_idx values to identify specific instances (like block IDs). Use 0x0/0xFFFF if not applicable or unknown.
 * Use depth_arg to trace error origin in call stack (0 for direct, increment for each layer).
 * Use constant naming for enums used in reporting to ensure consistency when want to reuse.
 ********************************************************************************************/



// --- Logging Macros (ISR Safe) ---
// If ENABLE_LOGGING is defined, maps to ESP_LOGx.
// Includes a check for ISR context to prevent crashes during interrupts.
#ifdef ENABLE_DEBUG_LOGS
    #define LOG_I(tag, fmt, ...) ESP_LOGI(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_E(tag, fmt, ...) ESP_LOGE(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_W(tag, fmt, ...) ESP_LOGW(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_D(tag, fmt, ...) ESP_LOGD(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    
    #define _EMU_LOG_SAFE(level_macro, tag, fmt, ...) \
        do { \
            if (!xPortInIsrContext()) { \
                level_macro(tag, fmt, ##__VA_ARGS__); \
            } \
        } while(0)
#else
    #define LOG_I(...)
    #define LOG_E(...)
    #define LOG_W(...)
    #define LOG_D(...)
    #define _EMU_LOG_SAFE(...)
#endif

// --- Queue Push Helper (ISR Safe) ---
// Automatically selects xQueueSend or xQueueSendFromISR based on context.
#define _EMU_PUSH_TO_QUEUE(_queue, _struct_ptr) \
    do { \
        if (xPortInIsrContext()) { \
            BaseType_t xHigherPriorityTaskWoken = pdFALSE; \
            xQueueSendFromISR(_queue, _struct_ptr, &xHigherPriorityTaskWoken); \
            /* We do not yield here to keep the ISR fast */ \
        } else { \
            xQueueSend(_queue, _struct_ptr, 0); \
        } \
    } while(0)

// --- Internal Error Builder ---
// populates emu_result_t and pushes it to the error queue.
#define _EMU_RETURN_ERR(_code, _owner_name_enum, _owner_custom_idx, _depth_arg, _is_notice, _is_warn, _is_abort) \
    do { \
        emu_result_t _err = { \
            .code = _code, \
            .owner = _owner_name_enum, \
            .owner_idx = _owner_custom_idx, /* Maps to owner_idx (Block ID) */ \
            .notice = _is_notice, \
            .warning = _is_warn, \
            .abort = _is_abort, \
            .depth = _depth_arg, \
            .time = emu_loop_get_time(), \
            .cycle = emu_loop_get_iteration() \
        }; \
        _EMU_PUSH_TO_QUEUE(error_logs_queue_t, &_err); \
        return _err; \
    } while(0)  

/*********************************************************************************************

                        PUBLIC MACOROS FOR REPORTING AND ERRORS

*******************************************************************************************/

    /**
     * @brief Critical: Code should handle this error and stop execution
     * @param code emu_err_t error
     * @param owner_name_enum "function name enum"
     * @param owner_idx "identifying index"
     * @param depth_arg To trace error origin
     * @param TAG log TAG
     * @param fmt standard log fmt
     */
#define EMU_RETURN_CRITICAL(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    do { \
        _EMU_LOG_SAFE(LOG_E, tag, "CRITICAL: " fmt, ##__VA_ARGS__); \
        _EMU_RETURN_ERR(code, owner_name_enum, owner_idx, depth_arg, 0, 0, 1); \
    } while(0)

    /**
     * @brief Warning: Suspicious state but execution continues (warning=1)
     * @param code emu_err_t error
     * @param owner_name_enum "function name enum"
     * @param owner_idx "identifying index"
     * @param depth_arg To trace error origin
     * @param TAG log TAG
     * @param fmt standard log fmt
     */
#define EMU_RETURN_WARN(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    do { \
        _EMU_LOG_SAFE(LOG_W, tag, "WARNING: " fmt, ##__VA_ARGS__); \
        _EMU_RETURN_ERR(code, owner_name_enum, owner_idx, depth_arg, 0, 1, 0); \
    } while(0)

    /**
     * @brief Notice: Informational event pushed to error queue (notice=1)
     * @param code emu_err_t error
     * @param owner_name_enum "function name enum"
     * @param owner_idx "identifying index"
     * @param depth_arg To trace error origin
     * @param TAG log TAG
     * @param fmt standard log fmt
     */
#define EMU_RETURN_NOTICE(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    do { \
        _EMU_LOG_SAFE(LOG_I, tag, "NOTICE: " fmt, ##__VA_ARGS__); \
        _EMU_RETURN_ERR(code, owner_name_enum, owner_idx, depth_arg, 1, 0, 0); \
    } while(0)

    /**
     * @brief Report an error without returning (no abort, warning, notice)
     * @param code emu_err_t error
     * @param owner_name_enum "function name enum"
     * @param owner_idx "identifying index"
     * @param depth_arg To trace error origin
     * @param TAG log TAG
     * @param fmt standard log fmt
     */

#define EMU_REPORT_ERROR_CRITICAL(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    do { \
        _EMU_LOG_SAFE(LOG_E, tag, "ERROR REPORTED: " fmt, ##__VA_ARGS__); \
        emu_result_t _err = { \
            .code = code_arg, \
            .owner = owner_name_enum, \
            .owner_idx = owner_idx_arg, \
            .notice = 0, \
            .warning = 0, \
            .abort = 1, \
            .depth = depth_arg, \
            .time = emu_loop_get_time(), \
            .cycle = emu_loop_get_iteration() \
        }; \
        _EMU_PUSH_TO_QUEUE(error_logs_queue_t, &_err); \
    } while(0)

#define EMU_REPORT_ERROR_WARN(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    do { \
        _EMU_LOG_SAFE(LOG_E, tag, "ERROR REPORTED: " fmt, ##__VA_ARGS__); \
        emu_result_t _err = { \
            .code = code_arg, \
            .owner = owner_name_enum, \
            .owner_idx = owner_idx_arg, \
            .notice = 0, \
            .warning = 1, \
            .abort = 0, \
            .depth = depth_arg, \
            .time = emu_loop_get_time(), \
            .cycle = emu_loop_get_iteration() \
        }; \
        _EMU_PUSH_TO_QUEUE(error_logs_queue_t, &_err); \
    } while(0)

#define EMU_REPORT_ERROR_NOTICE(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    do { \
        _EMU_LOG_SAFE(LOG_E, tag, "ERROR REPORTED: " fmt, ##__VA_ARGS__); \
        emu_result_t _err = { \
            .code = code_arg, \
            .owner = owner_name_enum, \
            .owner_idx = owner_idx_arg, \
            .notice = 1, \
            .warning = 0, \
            .abort = 0, \
            .depth = depth_arg, \
            .time = emu_loop_get_time(), \
            .cycle = emu_loop_get_iteration() \
        }; \
        _EMU_PUSH_TO_QUEUE(error_logs_queue_t, &_err); \
    } while(0)




#ifdef ENABLE_REPORT
    /* Returns EMU_OK and pushes a report to log_queue */
    /**
     * @brief Return EMU_OK code and push logs if enabled
     * @param log_msg_enum Log message as enum
     * @param owner_name_enum "function name enum"
     * @param owner_custom_idx "identifying index"
     * @param TAG log TAG
     * @param fmt standard log fmt
     */
    #define EMU_RETURN_OK(log_msg_enum, owner_name_enum, owner_custom_idx,  tag, fmt, ...) \
        do { \
            _EMU_LOG_SAFE(LOG_I, tag, "OK: " fmt, ##__VA_ARGS__); \
            emu_report_t _rep = { \
                .log = log_msg_enum, \
                .owner = owner_name_enum, \
                .owner_idx = owner_custom_idx, \
                .time = emu_loop_get_time(), \
                .cycle = emu_loop_get_iteration() \
            }; \
            _EMU_PUSH_TO_QUEUE(logs_queue_t, &_rep); \
            return (emu_result_t){ .code = EMU_OK }; \
        } while(0)

     /**
     * @brief Push logs if enabled (no return)
     * @param log_msg_enum Log message as enum
     * @param owner_name_enum "function name enum"
     * @param owner_custom_idx "identifying index"
     * @param TAG log TAG
     * @param fmt standard log fmt
     */
    #define EMU_REPORT(log_msg_enum, owner_name_enum, owner_custom_idx, tag, fmt, ...) \
        do { \
            _EMU_LOG_SAFE(LOG_I, tag, "OK: " fmt, ##__VA_ARGS__); \
            emu_report_t _rep = { \
                .log = log_msg_enum, \
                .owner = owner_name_enum, \
                .owner_idx = owner_custom_idx, \
                .time = emu_loop_get_time(), \
                .cycle = emu_loop_get_iteration() \
            }; \
            _EMU_PUSH_TO_QUEUE(logs_queue_t, &_rep); \
        } while(0)
#else
    #define EMU_RETURN_OK(owner_name_enum, log_msg_enum, tag, fmt, ...) \
        do { return (emu_result_t){ .code = EMU_OK }; } while(0)

    #define EMU_REPORT(...) do {} while(0)
#endif