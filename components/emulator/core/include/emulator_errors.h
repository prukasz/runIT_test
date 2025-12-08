#pragma once 

//comment to disable excess logging 
#define ENABLE_LOGGING 1

/**
 * @brief Debug logging for non essential code parts
 */
#ifdef ENABLE_LOGGING
    #define LOG_I(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
    #define LOG_E(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
    #define LOG_W(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
    #define LOG_D(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
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
    EMU_ERR_INVALID_STATE,
    EMU_ERR_INVALID_ARG,
    EMU_ERR_NO_MEMORY,
    EMU_ERR_ORD_START,
    EMU_ERR_ORD_CANNOT_EXECUTE,
    EMU_ERR_ORD_STOP,
    EMU_ERR_ORD_START_BLOCKS,
    EMU_ERR_INVALID_DATA,
    EMU_ERR_MEM_INVALID_DATATYPE,
    EMU_ERR_UNLIKELY           =0xFFFF,  
    EMU_ERR_MEM_ALLOC          =0xFF00,
    EMU_ERR_MEM_INVALID_INDEX  =0xFF01,
    EMU_ERR_MEM_INVALID_ACCESS =0xFF02,
    EMU_ERR_MEM_OUT_OF_BOUNDS  =0xFF03,
    EMU_ERR_NULL_POINTER       =0xFF04,   
    EMU_ERR_DIV_BY_ZERO        =0xBB01,
    EMU_ERR_OUT_OF_RANGE       =0xBB02,
    EMU_ERR_INVALID_PARAMETER  =0xBB03,
    EMU_ERR_PARSE_INVALID_REQUEST,
    EMU_ERR_DENY,
    EMU_ERR_CANT_FIND,
}emu_err_t;

const char* EMU_ERR_TO_STR(emu_err_t err_code);

#define EMU_RETURN_ON_ERROR(expr) ({ \
    emu_err_t _err = (expr);           \
    if (_err != EMU_OK) return _err;   \
    EMU_OK; \
})


