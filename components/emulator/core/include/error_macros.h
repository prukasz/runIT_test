#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <esp_log.h>
#include "error_types.h"
#include "emu_loop.h"
#include "emu_logs_config.h"

// --- Global Queue Handles ---
extern RingbufHandle_t error_logs_buff_t;
extern RingbufHandle_t status_logs_buff_t;


// Wrapper macros for logging with function name, can be disabled when not needed 
// Używamy ({ }) aby "dotknąć" zmiennych w bloku bez generowania kodu assemblera

#ifdef ENABLE_LOG_I
    #define LOG_I(tag, fmt, ...) ESP_LOGI(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else 
    #define LOG_I(tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif

#ifdef ENABLE_LOG_E 
    #define LOG_E(tag, fmt, ...) ESP_LOGE(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define LOG_E(tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif

#ifdef ENABLE_LOG_W
    #define LOG_W(tag, fmt, ...) ESP_LOGW(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define LOG_W(tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif

#ifdef ENABLE_LOG_D
    #define LOG_D(tag, fmt, ...) ESP_LOGD(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define LOG_D(tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif


#define _EMU_LOG_SAFE(log_x, tag, fmt, ...) ({ \
        if (likely(!xPortInIsrContext())) { \
            log_x(tag, fmt, ##__VA_ARGS__); \
        } \
    })

static __always_inline void _push_to_buf_overwrite(RingbufHandle_t rb, void *struct_ptr, size_t struct_size) {
    if (rb == NULL) return;

    if (unlikely(xPortInIsrContext())) {
        BaseType_t hpw = pdFALSE;
        // Try to send
        if (xRingbufferSendFromISR(rb, struct_ptr, struct_size, &hpw) != pdTRUE) {
            // If full, remove old item
            size_t isize = 0;
            void *old = xRingbufferReceiveFromISR(rb, &isize);
            if (old) {
                vRingbufferReturnItemFromISR(rb, old, &hpw);
                // Retry send
                (void)xRingbufferSendFromISR(rb, struct_ptr, struct_size, &hpw);
            }
        }
        if (hpw == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        // Task Context
        if (xRingbufferSend(rb, struct_ptr, struct_size, 0) != pdTRUE) {
            size_t isize = 0;
            void *old = xRingbufferReceive(rb, &isize, 0);
            if (old) {
                vRingbufferReturnItem(rb, old);
                (void)xRingbufferSend(rb, struct_ptr, struct_size, 0);
            }
        }
    }
}

#define _PUSH_TO_BUF(_rb, _struct_ptr) _push_to_buf_overwrite((_rb), (_struct_ptr), sizeof(*(_struct_ptr)))   

#ifdef ENABLE_ERROR_BUFF
    #define _TRY_ADD_ERROR(_err_ptr)  _PUSH_TO_BUF(error_logs_buff_t, (_err_ptr))
#else
    #define _TRY_ADD_ERROR(_err_ptr)  ({ (void)(_err_ptr); })
#endif

#ifdef ENABLE_STATUS_BUFF
    #define _TRY_ADD_STATUS(_rep_ptr)  _PUSH_TO_BUF(status_logs_buff_t, (_rep_ptr))
#else 
    #define _TRY_ADD_STATUS(_rep_ptr)  ({ (void)(_rep_ptr); })
#endif

#define _EMU_ADD_RET_ERR(_code, _owner, _idx, _depth, _is_notice, _is_warn, _is_abort) \
    ({ \
        emu_result_t _err = { \
            .code      = (_code), \
            .owner     = (_owner), \
            .owner_idx = (_idx),        /* Maps to owner_idx (Block ID) */ \
            .notice    = (_is_notice), \
            .warning   = (_is_warn), \
            .abort     = (_is_abort), \
            .depth     = (_depth), \
        }; \
        _TRY_ADD_ERROR(&_err); \
        return _err; \
    })

#define _EMU_ADD_ERR(_code, _owner, _idx, _depth, _is_notice, _is_warn, _is_abort) \
    ({ \
        emu_result_t _err = { \
            .code      = (_code), \
            .owner     = (_owner), \
            .owner_idx = (_idx),        /* Maps to owner_idx (Block ID) */ \
            .notice    = (_is_notice), \
            .warning   = (_is_warn), \
            .abort     = (_is_abort), \
            .depth     = (_depth), \
        }; \
        _TRY_ADD_ERROR(&_err); \
    })  

/*********************************************************************************************

                        PUBLIC MACOROS FOR REPORTING AND ERRORS

*******************************************************************************************/

#ifdef ENABLE_LOG_X_FROM_ERROR_MACROS
    #define _LOG_X_FROM_ERR(log_x, tag, fmt, ...) _EMU_LOG_SAFE(log_x, tag, fmt, ##__VA_ARGS__)
#else
    #define _LOG_X_FROM_ERR(log_x, tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif

#ifdef ENABLE_LOG_X_FROM_STATUS_MACROS
    #define _LOG_X_FROM_STAT(log_x, tag, fmt, ...) _EMU_LOG_SAFE(log_x, tag, fmt, ##__VA_ARGS__)
#else
    #define _LOG_X_FROM_STAT(log_x, tag, fmt, ...) ({ (void)(tag); (void)(fmt); })
#endif


    /**
     * @brief Critical: Code should handle this error and stop execution
     */
#define EMU_RETURN_CRITICAL(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    ({ \
        _LOG_X_FROM_ERR(LOG_E, tag, fmt, ##__VA_ARGS__); \
        _EMU_ADD_RET_ERR(code, owner_name_enum, owner_idx, depth_arg, 0, 0, 1); \
    })

    /**
     * @brief Warning: Suspicious state but execution continues (warning=1)
     */
#define EMU_RETURN_WARN(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    ({ \
        _LOG_X_FROM_ERR(LOG_W, tag, fmt, ##__VA_ARGS__); \
        _EMU_ADD_RET_ERR(code, owner_name_enum, owner_idx, depth_arg, 0, 1, 0); \
    })

    /**
     * @brief Notice: Informational event pushed to error queue (notice=1)
     */
#define EMU_RETURN_NOTICE(code, owner_name_enum, owner_idx, depth_arg, tag, fmt, ...) \
    ({ \
        _LOG_X_FROM_ERR(LOG_I, tag, "NOTICE: " fmt, ##__VA_ARGS__); \
        _EMU_ADD_RET_ERR(code, owner_name_enum, owner_idx, depth_arg, 1, 0, 0); \
    })

    /**
     * @brief Report an error without returning (no abort, warning, notice)
     */

#define EMU_REPORT_ERROR_CRITICAL(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    ({ \
        _LOG_X_FROM_ERR(LOG_E, tag, fmt, ##__VA_ARGS__); \
        _EMU_ADD_ERR(code_arg, owner_name_enum, owner_idx_arg, depth_arg, 0, 0, 1); \
    }) 

#define EMU_REPORT_ERROR_WARN(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    ({ \
        _LOG_X_FROM_ERR(LOG_W, tag, fmt, ##__VA_ARGS__); \
        _EMU_ADD_ERR(code_arg, owner_name_enum, owner_idx_arg, depth_arg, 0, 1, 0); \
    })

#define EMU_REPORT_ERROR_NOTICE(code_arg, owner_name_enum, owner_idx_arg, depth_arg, tag, fmt, ...)  \
    ({ \
        _LOG_X_FROM_ERR(LOG_I, tag, fmt, ##__VA_ARGS__); \
        _EMU_ADD_ERR(code_arg, owner_name_enum, owner_idx_arg, depth_arg, 1, 0, 0); \
    })


#ifdef ENABLE_STATUS_BUFF  

    #define EMU_RETURN_OK(log_msg_enum, owner_name_enum, owner_custom_idx,  tag, fmt, ...) \
        ({ \
            _LOG_X_FROM_STAT(LOG_I, tag, "OK: " fmt, ##__VA_ARGS__); \
            emu_report_t _rep = { \
                .log = log_msg_enum, \
                .owner = owner_name_enum, \
                .owner_idx = owner_custom_idx, \
            }; \
            _TRY_ADD_STATUS(&_rep); \
            return (emu_result_t){ .code = EMU_OK }; \
        })

    #define EMU_RETURN_OK_SILENT(log_msg_enum, owner_name_enum, owner_custom_idx) \
        ({ \
            emu_report_t _rep = { \
                .log = log_msg_enum, \
                .owner = owner_name_enum, \
                .owner_idx = owner_custom_idx, \
            }; \
            _TRY_ADD_STATUS(&_rep); \
            return (emu_result_t){ .code = EMU_OK }; \
        })

    #define EMU_REPORT(log_msg_enum, owner_name_enum, owner_custom_idx, tag, fmt, ...) \
        ({ \
            _LOG_X_FROM_STAT(LOG_I, tag, "OK: " fmt, ##__VA_ARGS__); \
            emu_report_t _rep = { \
                .log = log_msg_enum, \
                .owner = owner_name_enum, \
                .owner_idx = owner_custom_idx, \
            }; \
            _TRY_ADD_STATUS(&_rep); \
        })
#else

    #define EMU_RETURN_OK(log_msg_enum, owner_name_enum, owner_custom_idx, tag, fmt, ...) \
        ({ \
            (void)(log_msg_enum); \
            (void)(owner_name_enum); \
            (void)(owner_custom_idx); \
            (void)(tag); \
            (void)(fmt); \
            return (emu_result_t){ .code = EMU_OK }; \
        })
    #define EMU_RETURN_OK_SILENT(log_msg_enum, owner_name_enum, owner_custom_idx) \
        ({ \
            (void)(log_msg_enum); \
            (void)(owner_name_enum); \
            (void)(owner_custom_idx); \
            return (emu_result_t){ .code = EMU_OK }; \
        })

    #define EMU_REPORT(log_msg_enum, owner_name_enum, owner_custom_idx, tag, fmt, ...) \
        ({ \
            (void)(log_msg_enum); \
            (void)(owner_name_enum); \
            (void)(owner_custom_idx); \
            (void)(tag); \
            (void)(fmt); \
        })
#endif



/************************************************************************************************************ *
                                    PUBLIC API
* SHORTCUT MACROS FOR ERROR HANDLING "D": means with depth and block idx                *
************************************************************************************************************ */

/*emu_result_t with .code = EMU_OK*/
#define EMU_RESULT_OK() ((emu_result_t){ .code = EMU_OK })

/*Use when no details can be provided*/
#define RET_E(code, msg, ...) EMU_RETURN_CRITICAL(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Use when no details can be provided*/
#define RET_W(code, msg, ...) EMU_RETURN_WARN(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Use when no details can be provided*/
#define RET_N(code, msg, ...) EMU_RETURN_NOTICE(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Use when no details can be provided*/
#define RET_OK(msg, ...) EMU_RETURN_OK(EMU_LOG_finished, OWNER, 0xFFFF, TAG, msg, ##__VA_ARGS__)

/*Use when want to give block index and error depth*/
#define RET_ED(code, block_idx, depth, msg, ...) EMU_RETURN_CRITICAL(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Use when want to give block index and error depth*/
#define RET_WD(code, block_idx, depth, msg, ...) EMU_RETURN_WARN(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Use when want to give block index and error depth*/
#define RET_ND(code, block_idx, depth, msg, ...) EMU_RETURN_NOTICE(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Use when want to give block index and error depth*/
#define RET_OKD(block_idx, msg, ...) EMU_RETURN_OK(EMU_LOG_finished, OWNER, block_idx, TAG, msg, ##__VA_ARGS__)

/*Return when block inactive, no logging*/
#define RET_OK_INACTIVE(block_idx) EMU_RETURN_OK_SILENT(EMU_LOG_block_inactive, OWNER, block_idx)

/*Add error to queue, no return*/
#define REP_E(code, msg, ...) EMU_REPORT_ERROR_CRITICAL(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return*/
#define REP_W(code, msg, ...) EMU_REPORT_ERROR_WARN(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return*/
#define REP_N(code, msg, ...) EMU_REPORT_ERROR_NOTICE(code, OWNER, 0xFFFF, 0, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return*/
#define REP_OK(msg, ...) EMU_REPORT(EMU_LOG_finished, OWNER, 0xFFFF, TAG, msg, ##__VA_ARGS__)
/*Add status to queue, no return*/
#define REP_MSG(log_enum, idx,  msg, ...) EMU_REPORT(log_enum, OWNER, idx, TAG, msg, ##__VA_ARGS__)

/*Add error to queue, no return , detailed*/
#define REP_ED(code, block_idx, depth, msg, ...) EMU_REPORT_ERROR_CRITICAL(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return , detailed*/
#define REP_WD(code, block_idx, depth, msg, ...) EMU_REPORT_ERROR_WARN(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return , detailed*/
#define REP_ND(code, block_idx, depth, msg, ...) EMU_REPORT_ERROR_NOTICE(code, OWNER, block_idx, depth, TAG, msg, ##__VA_ARGS__)
/*Add error to queue, no return , detailed*/
#define REP_OKD(block_idx, msg, ...) EMU_REPORT(EMU_LOG_finished, OWNER, block_idx, TAG, msg, ##__VA_ARGS__)