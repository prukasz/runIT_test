#pragma once 
#include "stdint.h"
#include "esp_log.h"
//comment to disable excess logging 
//#define ENABLE_LOGGING 1

/**
 * @brief Debug logging for non essential code parts
 */
#ifdef ENABLE_LOGGING
    #define LOG_I(tag, fmt, ...) ESP_LOGI(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_E(tag, fmt, ...) ESP_LOGE(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_W(tag, fmt, ...) ESP_LOGW(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_D(tag, fmt, ...) ESP_LOGD(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define LOG_I(tag, fmt, ...) ((void)0)
    #define LOG_E(tag, fmt, ...) ((void)0)
    #define LOG_W(tag, fmt, ...) ((void)0)
    #define LOG_D(tag, fmt, ...) ((void)0)
#endif

// --- MACROS THAT LOG AND RETURN AUTOMATICALLY ---

// Usage: EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, block_idx, TAG, "Msg");
// (Do NOT put 'return' before this macro!)
#define EMU_RETURN_CRITICAL(_code, _block_id, _tag, _fmt, ...) \
    do { \
        LOG_E(_tag, _fmt, ##__VA_ARGS__); \
        return (emu_result_t){ \
            .code = _code, \
            .block_idx = _block_id, \
            .abort = 1 \
        }; \
    } while(0)

// Usage: EMU_RETURN_WARN(...)
#define EMU_RETURN_WARN(_code, _block_id, _tag, _fmt, ...) \
    do { \
        LOG_W(_tag, _fmt, ##__VA_ARGS__); \
        return (emu_result_t){ \
            .code = _code, \
            .block_idx = _block_id, \
            .warning = 1 \
        }; \
    } while(0)

// Usage: EMU_RETURN_NOTICE(...)
#define EMU_RETURN_NOTICE(_code, _block_id, _tag, _fmt, ...) \
    do { \
        LOG_I(_tag, _fmt, ##__VA_ARGS__); \
        return (emu_result_t){ \
            .code = _code, \
            .block_idx = _block_id, \
            .notice = 1 \
        }; \
    } while(0)

// Standard OK (No logging, used normally)
#define EMU_RESULT_OK() \
    (emu_result_t){ \
        .code = EMU_OK, \
        .block_idx = 0xFFFF, \
    }

typedef enum {
    EMU_OK = 0,

    /* --------------------------
     * EXECUTION / ORDER / PARSING ERRORS (0xE...)
     * -------------------------- */
    
    EMU_ERR_INVALID_STATE           = 0xE001,
    EMU_ERR_INVALID_ARG             = 0xE002,
    EMU_ERR_INVALID_DATA            = 0xE003,
    EMU_ERR_ORD_CANNOT_EXECUTE      = 0xE005,
    EMU_ERR_PARSE_INVALID_REQUEST   = 0xE008,
    EMU_ERR_DENY                    = 0xE009,
    EMU_ERR_PACKET_NOT_FOUND        = 0xE00A,
    EMU_ERR_UNLIKELY                = 0xEFFF,
    EMU_ERR_PACKET_INCOMPLETE       = 0xE00B,

    /* --------------------------
     * MEMORY ERRORS (0xF...)
     * -------------------------- */

    EMU_ERR_NO_MEM                  = 0xF000,
    EMU_ERR_MEM_ALLOC               = 0xF001,
    EMU_ERR_MEM_INVALID_IDX         = 0xF002,
    EMU_ERR_MEM_INVALID_ACCESS      = 0xF003,
    EMU_ERR_MEM_OUT_OF_BOUNDS       = 0xF004,
    EMU_ERR_MEM_INVALID_DATATYPE    = 0xF005,
    EMU_ERR_NULL_PTR                = 0xF006,
    EMU_ERR_MEM_INVALID_REF_ID      = 0xF007,


    /* --------------------------
     * BLOCK SPECIFIC ERRORS (0xB...)
     * -------------------------- */
    EMU_ERR_BLOCK_DIV_BY_ZERO       = 0xB001, 
    EMU_ERR_BLOCK_OUT_OF_RANGE      = 0xB002, 
    EMU_ERR_BLOCK_INVALID_PARAM     = 0xB003, 
    EMU_ERR_BLOCK_COMPUTE_IDX       = 0xB004,
    EMU_ERR_BLOCK_FOR_TIMEOUT       = 0xB005,
    EMU_ERR_BLOCK_INVALID_CONN      = 0xB006,
    EMU_ERR_BLOCK_ALREADY_FILLED    = 0xB007,
    EMU_ERR_BLOCK_WTD_TRIGGERED     = 0xB008,
    EMU_ERR_BLOCK_USE_INTERNAL_VAR  = 0xB009,
    EMU_ERR_BLOCK_INACTIVE          = 0xB00A,
    
    EMU_ERR_WTD_TRIGGERED           = 0xA000,
    

} emu_err_t;

typedef struct {
    emu_err_t   code;
    uint16_t    block_idx;   
    
    uint8_t     abort   : 1;
    uint8_t     restart : 1;
    uint8_t     warning : 1; 
    uint8_t     notice  : 1;
    uint8_t    _reserved: 4; // Padding
    
} emu_result_t;

const char* EMU_ERR_TO_STR(emu_err_t err_code);
const char* EMU_ORDER_TO_STR(uint16_t order);


