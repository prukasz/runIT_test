from enum import IntEnum

class emu_ord_t(IntEnum):
    #/********PARSER ORDERS **************/
    ORD_CREATE_CONTEXT        = 0xFFFF,  #//Create context with provided size
    ORD_PARSE_VARIABLES       = 0xEEEE,  #//Parse variables types and arrays sizes
    ORD_PARSE_VARIABLES_DATA  = 0xDDDD,  #//Fill created variables with provided data
    ORD_EMU_CREATE_BLOCK_LIST = 0xb100,  #//Create list for number of provided blocks (Total blocks in code)
    ORD_EMU_CREATE_BLOCKS     = 0xb200,  #//Create blocks (Inputs, Outputs, Type, Custom data)
    ORD_CHECK_CODE            = 0x0030,  #//Check code completeness before start (once after parsing finish)

    #/********RESET ORDERS  ***************/
    ORD_RESET_ALL             = 0x0001,  #//Brings emulator to startup state, provides way to eaisly send new code
    ORD_RESET_BLOCKS          = 0x0002,  #//Reset all blocks and theirs data
    ORD_RESET_MGS_BUF         = 0x0003,  #//Clear msg buffer
    
    #/********LOOP CONTROL****************/
    ORD_EMU_LOOP_START     = 0x1000, #//start loop / resume 
    ORD_EMU_LOOP_STOP      = 0x2000, #//stop loop aka pause
    ORD_EMU_LOOP_INIT      = 0x3000, #//init loop first time 

    #/********DEBUG OPTIONS **************/
    ORD_EMU_INIT_WITH_DBG  = 0x3333, #//init loop with debug
    ORD_EMU_SET_PERIOD     = 0x4000, #//change period of loop
    ORD_EMU_RUN_ONCE       = 0x5000, #//Run one cycle and wait for next order
    ORD_EMU_RUN_WITH_DEBUG = 0x6000, #//Run with debug (Dump after each cycle)
    ORD_EMU_RUN_ONE_STEP   = 0x7000, #//Run one block / one step (With debug)

class emu_variables_headers_t(IntEnum):
    #//[HEADER][ctx_id][9x][UINT16_T TOTAL_CNT_OF_SLOTS] [9x] [UINT16_T TOTAL_CNT_OF_INSTANCE],[9x] [UINT16_T TOTAL_CNT_OF_EXTRA_SPACE] 
    VAR_H_SIZES = 0xFF00,
    #//[HEADER][ctx_id][9x][UINT16_T scalar_cnt](in enum order)
    VAR_H_SCALAR_CNT = 0xFF01,
    #//[HEADER][ctx_id]N*([uint8_t dimscnt][uint8_t type][uint8_t idx])
    VAR_H_ARR = 0xFF02,

    #/*******************ARRAYS AND SCALARS DATA**************************************** */
    #//[HEADER][ctx_id], [ui16_t idx] [data], [ui16_t idx] [data], [ui16_t idx] [data] .. 
    VAR_H_DATA_S_UI8  = 0x0F10,
    VAR_H_DATA_S_UI16 = 0x0F20,
    VAR_H_DATA_S_UI32 = 0x0F30,
    VAR_H_DATA_S_I8   = 0x0F40,
    VAR_H_DATA_S_I16  = 0x0F50,
    VAR_H_DATA_S_I32  = 0x0F60,
    VAR_H_DATA_S_F    = 0x0F70,
    VAR_H_DATA_S_D    = 0x0F80,
    VAR_H_DATA_S_B    = 0x0F90,
    #//[HEADER][ctx_id], [ui16_t idx,][ui16_t in_arr_idx_offset][data](till end of packet)
    VAR_H_DATA_ARR_UI8  = 0xFFF0,
    VAR_H_DATA_ARR_UI16 = 0xFFF1,
    VAR_H_DATA_ARR_UI32 = 0xFFF2,
    VAR_H_DATA_ARR_I8   = 0xFFF3,
    VAR_H_DATA_ARR_I16  = 0xFFF4,
    VAR_H_DATA_ARR_I32  = 0xFFF5,
    VAR_H_DATA_ARR_F    = 0xFFF6,
    VAR_H_DATA_ARR_D    = 0xFFF7,
    VAR_H_DATA_ARR_B    = 0xFFF8,

class block_type_t(IntEnum):
    BLOCK_MATH       = 0x01,
    BLOCK_SET        = 0x02,
    BLOCK_LOGIC      = 0x03,
    BLOCK_FOR        = 0x08,
    BLOCK_TIMER      = 0x09,

class emu_block_header_t(IntEnum):
    EMU_H_BLOCK_CNT       = 0xB000,
    EMU_H_BLOCK_START     = 0xBB,

    EMU_H_BLOCK_PACKET_IN_START  = 0xF0,
    EMU_H_BLOCK_PACKET_OUT_START = 0xE0,
    EMU_H_BLOCK_PACKET_CFG       = 0x00,
    EMU_H_BLOCK_PACKET_CONST     = 0x01,
    EMU_H_BLOCK_PACKET_CUSTOM    = 0x02,


class emu_types_t(IntEnum):
    DATA_UI8  = 0x00,
    DATA_UI16 = 0x01,
    DATA_UI32 = 0x02,
    DATA_I8   = 0x03,
    DATA_I16  = 0x04,
    DATA_I32  = 0x05,
    DATA_F    = 0x06,
    DATA_D    = 0x07,
    DATA_B    = 0x08,

class emu_err_t(IntEnum):
    EMU_OK = 0,

    #/* --------------------------
    # * EXECUTION / ORDER / PARSING ERRORS (0xE...)
    # * -------------------------- */
    
    EMU_ERR_INVALID_STATE           = 0xE001,
    EMU_ERR_INVALID_ARG             = 0xE002,
    EMU_ERR_INVALID_DATA            = 0xE003,
    EMU_ERR_ORD_CANNOT_EXECUTE      = 0xE005,
    EMU_ERR_PARSE_INVALID_REQUEST   = 0xE008,
    EMU_ERR_DENY                    = 0xE009,
    EMU_ERR_PACKET_NOT_FOUND        = 0xE00A,
    EMU_ERR_UNLIKELY                = 0xEFFF,
    EMU_ERR_PACKET_INCOMPLETE       = 0xE00B,

    #/* --------------------------
    # * MEMORY ERRORS (0xF...)
    # * -------------------------- */

    EMU_ERR_NO_MEM                  = 0xF000,
    EMU_ERR_MEM_ALLOC               = 0xF001,
    EMU_ERR_MEM_INVALID_IDX         = 0xF002,
    EMU_ERR_MEM_INVALID_ACCESS      = 0xF003,
    EMU_ERR_MEM_OUT_OF_BOUNDS       = 0xF004,
    EMU_ERR_MEM_INVALID_DATATYPE    = 0xF005,
    EMU_ERR_NULL_PTR                = 0xF006,
    EMU_ERR_MEM_INVALID_REF_ID      = 0xF007,


    #/* --------------------------
    # * BLOCK SPECIFIC ERRORS (0xB...)
    # * -------------------------- */
    
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

Headers = emu_variables_headers_t
DataTypes = emu_types_t

TYPE_CONFIG = {
    DataTypes.DATA_UI8:  {"fmt": "B", "size": 1, "h_scal": Headers.VAR_H_DATA_S_UI8,  "h_arr": Headers.VAR_H_DATA_ARR_UI8},
    DataTypes.DATA_UI16: {"fmt": "H", "size": 2, "h_scal": Headers.VAR_H_DATA_S_UI16, "h_arr": Headers.VAR_H_DATA_ARR_UI16},
    DataTypes.DATA_UI32: {"fmt": "I", "size": 4, "h_scal": Headers.VAR_H_DATA_S_UI32, "h_arr": Headers.VAR_H_DATA_ARR_UI32},
    DataTypes.DATA_I8:   {"fmt": "b", "size": 1, "h_scal": Headers.VAR_H_DATA_S_I8,   "h_arr": Headers.VAR_H_DATA_ARR_I8},
    DataTypes.DATA_I16:  {"fmt": "h", "size": 2, "h_scal": Headers.VAR_H_DATA_S_I16,  "h_arr": Headers.VAR_H_DATA_ARR_I16},
    DataTypes.DATA_I32:  {"fmt": "i", "size": 4, "h_scal": Headers.VAR_H_DATA_S_I32,  "h_arr": Headers.VAR_H_DATA_ARR_I32},
    DataTypes.DATA_F:    {"fmt": "f", "size": 4, "h_scal": Headers.VAR_H_DATA_S_F,    "h_arr": Headers.VAR_H_DATA_ARR_F},
    DataTypes.DATA_D:    {"fmt": "d", "size": 8, "h_scal": Headers.VAR_H_DATA_S_D,    "h_arr": Headers.VAR_H_DATA_ARR_D},
    DataTypes.DATA_B:    {"fmt": "?", "size": 1, "h_scal": Headers.VAR_H_DATA_S_B,    "h_arr": Headers.VAR_H_DATA_ARR_B},
}
