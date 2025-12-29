#pragma once 
#include "stdint.h"
//comment to disable excess logging 
#define ENABLE_LOGGING 1

/**
 * @brief Debug logging for non essential code parts
 */
#ifdef ENABLE_LOGGING
    #define LOG_I(tag, fmt, ...) ESP_LOGI(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_E(tag, fmt, ...) ESP_LOGE(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_W(tag, fmt, ...) ESP_LOGW(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
    #define LOG_D(tag, fmt, ...) ESP_LOGD(tag, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define LOG_I(tag, fmt, ...) do {} while(0)
    #define LOG_E(tag, fmt, ...) do {} while(0)
    #define LOG_W(tag, fmt, ...) do {} while(0)
    #define LOG_D(tag, fmt, ...) do {} while(0)
#endif

/**
* @brief Emulator scope error codes
*/
typedef enum {
    EMU_OK = 0,

    /* --------------------------
     * EXECUTION / ORDER / PARSING ERRORS (0xE...)
     * -------------------------- */
    
    EMU_ERR_INVALID_STATE           = 0xE001,
    EMU_ERR_INVALID_ARG             = 0xE002,
    EMU_ERR_INVALID_DATA            = 0xE003,
    EMU_ERR_ORD_START               = 0xE004,
    EMU_ERR_ORD_CANNOT_EXECUTE      = 0xE005,
    EMU_ERR_ORD_STOP                = 0xE006,
    EMU_ERR_ORD_START_BLOCKS        = 0xE007,
    EMU_ERR_PARSE_INVALID_REQUEST   = 0xE008,
    EMU_ERR_DENY                    = 0xE009,
    EMU_ERR_NOT_FOUND               = 0xE00A,
    EMU_ERR_UNLIKELY                = 0xEFFF,

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

} emu_err_t;

typedef struct {
    emu_err_t   code;
    uint16_t    block_idx;   
    
    uint8_t     abort   : 1;
    uint8_t     restart : 1;
    uint8_t     warning : 1; 
    uint8_t     notice  : 1;
    uint8_t     reserved: 4; // Padding
    
} emu_result_t;

const char* EMU_ERR_TO_STR(emu_err_t err_code);

#define EMU_RESULT_CRITICAL(_code, _block_id) \
    (emu_result_t){ \
        .code = _code, \
        .block_idx = _block_id, \
        .abort = 1 \
    }

#define EMU_RESULT_WARN(_code, _block_id) \
    (emu_result_t){ \
        .code = _code, \
        .block_idx = _block_id, \
        .warning = 1 \
    }

#define EMU_RESULT_OK() \
    (emu_result_t){ \
        .code = EMU_OK, \
        .block_idx = 0xFFFF, \
    }


#define EMU_RETURN_ON_ERROR(expr) ({ \
    emu_err_t _err = (expr);           \
    if (_err != EMU_OK) return _err;   \
    EMU_OK; \
})

extern const char *DATA_TYPE_TO_STR[9];

