#include "emulator_errors.h"
#include "emulator_types.h" 

/**
 * @brief Return string representation of emu_err_t code
 */
const char* EMU_ERR_TO_STR(emu_err_t err_code) {
    switch (err_code) {
        case EMU_OK:                       return "EMU_OK";
        case EMU_ERR_INVALID_STATE:        return "EMU_ERR_INVALID_STATE";
        case EMU_ERR_INVALID_ARG:          return "EMU_ERR_INVALID_ARG";
        case EMU_ERR_NO_MEMORY:            return "EMU_ERR_NO_MEMORY";
        case EMU_ERR_ORD_START:            return "EMU_ERR_ORD_START";
        case EMU_ERR_ORD_CANNOT_EXECUTE:   return "EMU_ERR_ORD_CANNOT_EXECUTE";
        case EMU_ERR_ORD_STOP:             return "EMU_ERR_ORD_STOP";
        case EMU_ERR_ORD_START_BLOCKS:     return "EMU_ERR_ORD_START_BLOCKS";
        case EMU_ERR_INVALID_DATA:         return "EMU_ERR_INVALID_DATA";
        case EMU_ERR_MEM_INVALID_DATATYPE: return "EMU_ERR_MEM_INVALID_DATATYPE";
        case EMU_ERR_DIV_BY_ZERO:          return "EMU_ERR_DIV_BY_ZERO";
        case EMU_ERR_OUT_OF_RANGE:         return "EMU_ERR_OUT_OF_RANGE";
        case EMU_ERR_INVALID_PARAMETER:    return "EMU_ERR_INVALID_PARAMETER";
        case EMU_ERR_PARSE_INVALID_REQUEST:return "EMU_ERR_PARSE_INVALID_REQUEST";
        case EMU_ERR_DENY:                 return "EMU_ERR_DENY";
        case EMU_ERR_CANT_FIND:            return "EMU_ERR_CANT_FIND";
        case EMU_ERR_MEM_ALLOC:            return "EMU_ERR_MEM_ALLOC";
        case EMU_ERR_MEM_INVALID_INDEX:    return "EMU_ERR_MEM_INVALID_INDEX";
        case EMU_ERR_MEM_INVALID_ACCESS:   return "EMU_ERR_MEM_INVALID_ACCESS";
        case EMU_ERR_MEM_OUT_OF_BOUNDS:    return "EMU_ERR_MEM_OUT_OF_BOUNDS";
        case EMU_ERR_NULL_POINTER:         return "EMU_ERR_NULL_POINTER";
        case EMU_ERR_UNLIKELY:             return "EMU_ERR_UNLIKELY";

        default:                           return "UNKNOWN_HEX_CODE";
    }
}