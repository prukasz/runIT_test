#include "emulator_errors.h"
#include "emulator_types.h" 

/**
 * @brief Return string representation of emu_err_t code
 */
const char* EMU_ERR_TO_STR(emu_err_t err_code) {
    switch (err_code) {
        // --- OK ---
        case EMU_OK:                        return "EMU_OK";

        // --- EXECUTION / ORDER / PARSING (0xE...) ---
        case EMU_ERR_INVALID_STATE:         return "EMU_ERR_INVALID_STATE";
        case EMU_ERR_INVALID_ARG:           return "EMU_ERR_INVALID_ARG";
        case EMU_ERR_INVALID_DATA:          return "EMU_ERR_INVALID_DATA";
        case EMU_ERR_ORD_CANNOT_EXECUTE:    return "EMU_ERR_ORD_CANNOT_EXECUTE";
        case EMU_ERR_PARSE_INVALID_REQUEST: return "EMU_ERR_PARSE_INVALID_REQUEST";
        case EMU_ERR_DENY:                  return "EMU_ERR_DENY";
        case EMU_ERR_PACKET_NOT_FOUND:      return "EMU_ERR_PACKET_NOT_FOUND";
        case EMU_ERR_PACKET_INCOMPLETE:     return "EMU_ERR_PACKET_INCOMPLETE";
        case EMU_ERR_UNLIKELY:              return "EMU_ERR_UNLIKELY";

        // --- MEMORY (0xF...) ---
        case EMU_ERR_NO_MEM:                return "EMU_ERR_NO_MEM";
        case EMU_ERR_MEM_ALLOC:             return "EMU_ERR_MEM_ALLOC";
        case EMU_ERR_MEM_INVALID_IDX:       return "EMU_ERR_MEM_INVALID_IDX";
        case EMU_ERR_MEM_INVALID_ACCESS:    return "EMU_ERR_MEM_INVALID_ACCESS";
        case EMU_ERR_MEM_OUT_OF_BOUNDS:     return "EMU_ERR_MEM_OUT_OF_BOUNDS";
        case EMU_ERR_MEM_INVALID_DATATYPE:  return "EMU_ERR_MEM_INVALID_DATATYPE";
        case EMU_ERR_NULL_PTR:              return "EMU_ERR_NULL_PTR";
        case EMU_ERR_MEM_INVALID_REF_ID:    return "EMU_ERR_MEM_INVALID_REF_ID";

        // --- BLOCK SPECIFIC (0xB...) ---
        case EMU_ERR_BLOCK_DIV_BY_ZERO:     return "EMU_ERR_BLOCK_DIV_BY_ZERO";
        case EMU_ERR_BLOCK_OUT_OF_RANGE:    return "EMU_ERR_BLOCK_OUT_OF_RANGE";
        case EMU_ERR_BLOCK_INVALID_PARAM:   return "EMU_ERR_BLOCK_INVALID_PARAM";
        case EMU_ERR_BLOCK_COMPUTE_IDX:     return "EMU_ERR_BLOCK_COMPUTE_IDX";
        case EMU_ERR_BLOCK_FOR_TIMEOUT:     return "EMU_ERR_BLOCK_FOR_TIMEOUT";
        case EMU_ERR_BLOCK_INVALID_CONN:    return "EMU_ERR_BLOCK_INVALID_CONN";
        case EMU_ERR_BLOCK_ALREADY_FILLED:  return "EMU_ERR_BLOCK_ALREADY_FILLED";
        case EMU_ERR_BLOCK_WTD_TRIGGERED:   return "EMU_ERR_BLOCK_WTD_TRIGGERED";
        case EMU_ERR_BLOCK_USE_INTERNAL_VAR:return "EMU_ERR_BLOCK_USE_INTERNAL_VAR";
        case EMU_ERR_BLOCK_INACTIVE:        return "EMU_ERR_BLOCK_INACTIVE";

        
        case EMU_ERR_WTD_TRIGGERED:         return "EMU_ERR_WTD_TRIGGERED";

        default:                            return "UNKNOWN_ERR_CODE";
    }
}

/**
 * @brief Return string representation of emu_order_t code
 */
const char* EMU_ORDER_TO_STR(uint16_t order) {
    switch (order) {
        // --- PARSER ORDERS ---
        case ORD_CREATE_CONTEXT:        return "ORD_CREATE_CONTEXT";
        case ORD_PARSE_VARIABLES:       return "ORD_PARSE_VARIABLES";
        case ORD_PARSE_VARIABLES_DATA:  return "ORD_PARSE_VARIABLES_DATA";
        case ORD_EMU_CREATE_BLOCK_LIST: return "ORD_EMU_CREATE_BLOCK_LIST";
        case ORD_EMU_CREATE_BLOCKS:     return "ORD_EMU_CREATE_BLOCKS";
        case ORD_CHECK_CODE:            return "ORD_CHECK_CODE";

        // --- RESET ORDERS ---
        case ORD_RESET_ALL:             return "ORD_RESET_ALL";
        case ORD_RESET_BLOCKS:          return "ORD_RESET_BLOCKS";
        case ORD_RESET_MGS_BUF:         return "ORD_RESET_MGS_BUF";

        // --- LOOP CONTROL ---
        case ORD_EMU_LOOP_START:        return "ORD_EMU_LOOP_START";
        case ORD_EMU_LOOP_STOP:         return "ORD_EMU_LOOP_STOP";
        case ORD_EMU_LOOP_INIT:         return "ORD_EMU_LOOP_INIT";

        // --- DEBUG OPTIONS ---
        case ORD_EMU_INIT_WITH_DBG:     return "ORD_EMU_INIT_WITH_DBG";
        case ORD_EMU_SET_PERIOD:        return "ORD_EMU_SET_PERIOD";
        case ORD_EMU_RUN_ONCE:          return "ORD_EMU_RUN_ONCE";
        case ORD_EMU_RUN_WITH_DEBUG:    return "ORD_EMU_RUN_WITH_DEBUG";
        case ORD_EMU_RUN_ONE_STEP:      return "ORD_EMU_RUN_ONE_STEP";

        default:                        return "UNKNOWN_ORDER";
    }
}

