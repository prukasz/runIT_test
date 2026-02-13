import ctypes as ct 
from enum import IntEnum
from struct import pack
from typing import Any

class mem_types_t(IntEnum):
    MEM_U8   = 0
    MEM_U16  = 1
    MEM_U32  = 2
    MEM_I16  = 3
    MEM_I32  = 4
    MEM_B    = 5
    MEM_F    = 6


mem_types_map: dict[mem_types_t, Any] = {
    mem_types_t.MEM_U8:  ct.c_uint8,
    mem_types_t.MEM_U16: ct.c_uint16,
    mem_types_t.MEM_U32: ct.c_uint32,
    mem_types_t.MEM_I16: ct.c_int16,
    mem_types_t.MEM_I32: ct.c_int32,
    mem_types_t.MEM_B:   ct.c_bool,
    mem_types_t.MEM_F:   ct.c_float,
}

mem_types_size: dict[mem_types_t, int ] = {
    mem_types_t.MEM_U8:  ct.sizeof(ct.c_uint8),
    mem_types_t.MEM_U16: ct.sizeof(ct.c_uint16),
    mem_types_t.MEM_U32: ct.sizeof(ct.c_uint32),
    mem_types_t.MEM_I16: ct.sizeof(ct.c_int16),
    mem_types_t.MEM_I32: ct.sizeof(ct.c_int32),
    mem_types_t.MEM_B:   ct.sizeof(ct.c_bool),
    mem_types_t.MEM_F:   ct.sizeof(ct.c_float),
}

class packet_header_t(IntEnum):
    PACKET_H_CONTEXT_CFG          = 0xF0,
    PACKET_H_INSTANCE             = 0xF1,
    PACKET_H_INSTANCE_SCALAR_DATA = 0xFA,
    PACKET_H_INSTANCE_ARR_DATA    = 0xFB,

    PACKET_H_LOOP_CFG             = 0xA0,
    PACKET_H_CODE_CFG             = 0xAA,

    PACKET_H_BLOCK_HEADER         = 0xB0,
    PACKET_H_BLOCK_INPUTS         = 0xB1,
    PACKET_H_BLOCK_OUTPUTS        = 0xB2,
    PACKET_H_BLOCK_DATA           = 0xBA,

    PACKET_H_SUBSCRIPTION_INIT    = 0xC0,
    PACKET_H_SUBSCRIPTION_ADD     = 0xC1,

    PACKET_H_PUBLISH              = 0xD0,

class emu_order_t(IntEnum):
    #/********PARSER ORDERS **************/
    ORD_PARSE_CONTEXT_CFG        = 0xAAF0,  #/*Parse and create context*/
    ORD_PARSE_VARIABLES          = 0xAAF1,  #//Parse and create variables instances 
    ORD_PARSE_VARIABLES_S_DATA   = 0xAAFA,  #//Fill created scalar variables with data
    ORD_PARSE_VARIABLES_ARR_DATA = 0xAAFB, 

    ORD_PARSE_LOOP_CFG           = 0xAAA0,   #  //Create loop with provided config
    ORD_PARSE_CODE_CFG           = 0xAAAA,  #//Create code context with provided config (block list)

    ORD_PARSE_BLOCK_HEADER       = 0xAAB0,
    ORD_PARSE_BLOCK_INPUTS       = 0xAAB1,
    ORD_PARSE_BLOCK_OUTPUTS      = 0xAAB2,
    ORD_PARSE_BLOCK_DATA         = 0xAABA,

    ORD_PARSE_SUBSCRIPTION_INIT   = 0xAAC0, #//Initialize subscription system
    ORD_PARSE_SUBSCRIPTION_ADD     = 0xAAC1, #//Add subscription entries

    ORD_PARSE_RESET_STATUS       = 0xAA00, #//Reset parser status to initial state (for new code parsing)

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

mem_types_pack_map: dict[mem_types_t, str] = {
    mem_types_t.MEM_U8:  '<B',
    mem_types_t.MEM_U16: '<H',
    mem_types_t.MEM_U32: '<I',
    mem_types_t.MEM_I16: '<h',
    mem_types_t.MEM_I32: '<i',
    mem_types_t.MEM_B:   '<?',
    mem_types_t.MEM_F:   '<f',
}

class block_types_t(IntEnum):
    """Block type identification code - matches C block_type_t"""
    BLOCK_MATH        = 0x01
    BLOCK_SET         = 0x02
    BLOCK_LOGIC       = 0x03
    BLOCK_COUNTER     = 0x04
    BLOCK_CLOCK       = 0x05
    BLOCK_FOR         = 0x08
    BLOCK_TIMER       = 0x09
    BLOCK_IN_SELECTOR = 0x0A
    BLOCK_Q_SELECTOR  = 0x0B
    BLOCK_LATCH       = 0x0C


class block_packet_id_t(IntEnum):
    """
    Block data packet IDs - identifies type of data in PACKET_H_BLOCK_DATA packets.
    
    Packet format: [BA][block_idx:u16][block_type:u8][packet_id:u8][data...]
    
    Data packets (0x00-0x0F):
    - 0x00: Constants (cnt + floats)
    - 0x01: Block configuration
    
    Code/expression packets (0x10-0x1F):
    - 0x10: Instructions bytecode
    
    Dynamic option packets (0x20+):
    - 0x20+n: Selector options
    """
    # Data packets (0x00-0x0F)
    PKT_CONSTANTS      = 0x00  # Constants: [cnt:u8][float...]
    PKT_CFG            = 0x01  # Configuration: block-specific config data
    
    # Code/expression packets (0x10-0x1F)
    PKT_INSTRUCTIONS   = 0x10  # Instructions: bytecode for Math, Logic blocks
    
    # Dynamic option packets (0x20+)
    PKT_OPTIONS_BASE   = 0x20  # Selector options: 0x20 + option_index

