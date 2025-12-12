from enum import IntEnum 
class DataTypes(IntEnum):
    """Corresponds to the C enum data_types_t."""
    DATA_UI8  = 0
    DATA_UI16 = 1
    DATA_UI32 = 2
    DATA_I8   = 3
    DATA_I16  = 4
    DATA_I32  = 5
    DATA_F    = 6
    DATA_D    = 7
    DATA_B    = 8

class BlockTypes(IntEnum):
    """Corresponds to the C enum block_type_t."""
    BLOCK_MATH = 1
    BOCK_GLOBAL_SET = 254

class Order(IntEnum):
    ORD_STOP_BYTES           = 0x0000
    ORD_START_BYTES          = 0xFFFF
    ORD_PARSE_INTO_VARIABLES = 0xDDDD
    ORD_START_BLOCKS         = 0x00FF
    ORD_H_VARIABLES          = 0xFF00
    ORD_PARSE_VARIABLES      = 0xEEEE
    ORD_RESET_ALL            = 0x0001 
    ORD_RESET_BLOCKS         = 0x0002
    ORD_RESET_MGS_BUF        = 0x0003 
    ORD_PROCESS_CODE         = 0x0020
    ORD_CHECK_CODE           = 0x0030
    ORD_EMU_LOOP_START       = 0x1000
    ORD_EMU_LOOP_STOP        = 0x2000
    ORD_EMU_LOOP_INIT        = 0x2137
    ORD_EMU_ALLOCATE_BLOCKS_LIST = 0xb100
    ORD_EMU_FILL_BLOCKS_LIST     = 0xb200


class Headers(IntEnum):
    """Specific block headers for Scalars and Arrays."""
    # Unsigned 8-bit
    H_TABLE_UI8  = 0xFF10
    H_SCALAR_UI8 = 0xFF11
    # Unsigned 16-bit
    H_TABLE_UI16 = 0xFF20
    H_SCALAR_UI16= 0xFF21
    # Unsigned 32-bit
    H_TABLE_UI32 = 0xFF30
    H_SCALAR_UI32= 0xFF31
    # Signed 8-bit
    H_TABLE_I8   = 0xFF40
    H_SCALAR_I8  = 0xFF41
    # Signed 16-bit
    H_TABLE_I16  = 0xFF50
    H_SCALAR_I16 = 0xFF51
    # Signed 32-bit
    H_TABLE_I32  = 0xFF60
    H_SCALAR_I32 = 0xFF61
    # Float
    H_TABLE_F    = 0xFF70
    H_SCALAR_F   = 0xFF71
    # Double
    H_TABLE_D    = 0xFF80
    H_SCALAR_D   = 0xFF81
    # Boolean
    H_TABLE_B    = 0xFF90
    H_SCALAR_B   = 0xFF91

    H_START_REFERENCE = 0xF0
    H_BLOCKS_CNT = 0xB000