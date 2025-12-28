#include "emulator_errors.h"
#include "emulator_types.h" 



/**
 * @brief Return string representation of emu_err_t code
 */
const char* EMU_ERR_TO_STR(emu_err_t err_code) {
    switch (err_code) {
        // OK
        case EMU_OK:                        return "EMU_OK";

        // Execution / Order / Parsing
        case EMU_ERR_INVALID_STATE:         return "EMU_ERR_INVALID_STATE";
        case EMU_ERR_INVALID_ARG:           return "EMU_ERR_INVALID_ARG";
        case EMU_ERR_INVALID_DATA:          return "EMU_ERR_INVALID_DATA";
        case EMU_ERR_ORD_START:             return "EMU_ERR_ORD_START";
        case EMU_ERR_ORD_CANNOT_EXECUTE:    return "EMU_ERR_ORD_CANNOT_EXECUTE";
        case EMU_ERR_ORD_STOP:              return "EMU_ERR_ORD_STOP";
        case EMU_ERR_ORD_START_BLOCKS:      return "EMU_ERR_ORD_START_BLOCKS";
        case EMU_ERR_PARSE_INVALID_REQUEST: return "EMU_ERR_PARSE_INVALID_REQUEST";
        case EMU_ERR_DENY:                  return "EMU_ERR_DENY";
        case EMU_ERR_NOT_FOUND:             return "EMU_ERR_NOT_FOUND";
        case EMU_ERR_UNLIKELY:              return "EMU_ERR_UNLIKELY";

        // Memory
        case EMU_ERR_NO_MEM:                return "EMU_ERR_NO_MEM";
        case EMU_ERR_MEM_ALLOC:             return "EMU_ERR_MEM_ALLOC";
        case EMU_ERR_MEM_INVALID_IDX:       return "EMU_ERR_MEM_INVALID_IDX";
        case EMU_ERR_MEM_INVALID_ACCESS:    return "EMU_ERR_MEM_INVALID_ACCESS";
        case EMU_ERR_MEM_OUT_OF_BOUNDS:     return "EMU_ERR_MEM_OUT_OF_BOUNDS";
        case EMU_ERR_MEM_INVALID_DATATYPE:  return "EMU_ERR_MEM_INVALID_DATATYPE";
        case EMU_ERR_NULL_PTR:              return "EMU_ERR_NULL_PTR";

        // Block Specific
        case EMU_ERR_BLOCK_DIV_BY_ZERO:     return "EMU_ERR_BLOCK_DIV_BY_ZERO";
        case EMU_ERR_BLOCK_OUT_OF_RANGE:    return "EMU_ERR_BLOCK_OUT_OF_RANGE";
        case EMU_ERR_BLOCK_INVALID_PARAM:   return "EMU_ERR_BLOCK_INVALID_PARAM";
        case EMU_ERR_BLOCK_COMPUTE_IDX:     return "EMU_ERR_BLOCK_COMPUTE_IDX";
        case EMU_ERR_BLOCK_FOR_TIMEOUT:     return "EMU_ERR_BLOCK_FOR_TIMEOUT";
        case EMU_ERR_BLOCK_INVALID_CONN:    return "EMU_ERR_BLOCK_INVALID_CONN";

        default:                            return "UNKNOWN_ERR_CODE";
    }
}

const char *DATA_TYPE_TO_STR[9] = {
    "DATA_UI8",
    "DATA_UI16",
    "DATA_UI32",
    "DATA_I8",
    "DATA_I16",
    "DATA_I32",
    "DATA_F",
    "DATA_D",
    "DATA_B"
};