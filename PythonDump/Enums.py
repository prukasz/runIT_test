"""
Auto-generated from C header files.
DO NOT EDIT MANUALLY - Run sync_enums_from_c.py instead.
"""
import ctypes as ct
from enum import IntEnum
from typing import Any


class mem_types_t(IntEnum):
    """Memory types used within contexts"""
    MEM_U8                           = 0
    MEM_U16                          = 1
    MEM_U32                          = 2
    MEM_I16                          = 3
    MEM_I32                          = 4
    MEM_B                            = 5
    MEM_F                            = 6



mem_types_map: dict[mem_types_t, Any] = {
    mem_types_t.MEM_U8:  ct.c_uint8,
    mem_types_t.MEM_U16: ct.c_uint16,
    mem_types_t.MEM_U32: ct.c_uint32,
    mem_types_t.MEM_I16: ct.c_int16,
    mem_types_t.MEM_I32: ct.c_int32,
    mem_types_t.MEM_B:   ct.c_bool,
    mem_types_t.MEM_F:   ct.c_float,
}

mem_types_size: dict[mem_types_t, int] = {
    mem_types_t.MEM_U8:  ct.sizeof(ct.c_uint8),
    mem_types_t.MEM_U16: ct.sizeof(ct.c_uint16),
    mem_types_t.MEM_U32: ct.sizeof(ct.c_uint32),
    mem_types_t.MEM_I16: ct.sizeof(ct.c_int16),
    mem_types_t.MEM_I32: ct.sizeof(ct.c_int32),
    mem_types_t.MEM_B:   ct.sizeof(ct.c_bool),
    mem_types_t.MEM_F:   ct.sizeof(ct.c_float),
}

mem_types_pack_map: dict[mem_types_t, str] = {
    mem_types_t.MEM_U8:  '<B',
    mem_types_t.MEM_U16: '<H',
    mem_types_t.MEM_U32: '<I',
    mem_types_t.MEM_I16: '<h',
    mem_types_t.MEM_I32: '<i',
    mem_types_t.MEM_B:   '<?',
    mem_types_t.MEM_F:   '<f',
}

mem_types_to_str_map: dict[mem_types_t, str] = {
    mem_types_t.MEM_U8:  'MEM_U8',
    mem_types_t.MEM_U16: 'MEM_U16',
    mem_types_t.MEM_U32: 'MEM_U32',
    mem_types_t.MEM_I16: 'MEM_I16',
    mem_types_t.MEM_I32: 'MEM_I32',
    mem_types_t.MEM_B:   'MEM_B',
    mem_types_t.MEM_F:   'MEM_F',
}


class packet_header_t(IntEnum):
    """Packet header types for emulator communication"""
    PACKET_H_CONTEXT_CFG             = 0xF0
    PACKET_H_INSTANCE                = 0xF1
    PACKET_H_INSTANCE_SCALAR_DATA    = 0xFA
    PACKET_H_INSTANCE_ARR_DATA       = 0xFB
    PACKET_H_LOOP_CFG                = 0xA0
    PACKET_H_CODE_CFG                = 0xAA
    PACKET_H_BLOCK_HEADER            = 0xB0
    PACKET_H_BLOCK_INPUTS            = 0xB1
    PACKET_H_BLOCK_OUTPUTS           = 0xB2
    PACKET_H_BLOCK_DATA              = 0xBA
    PACKET_H_SUBSCRIPTION_INIT       = 0xC0
    PACKET_H_SUBSCRIPTION_ADD        = 0xC1
    PACKET_H_PUBLISH                 = 0xD0
    PACKET_H_ERROR_LOG               = 0xE1
    PACKET_H_STATUS_LOG              = 0xE0



class emu_order_t(IntEnum):
    """Emulator orders for control and parsing"""
    ORD_PARSE_CONTEXT_CFG            = 0xAAF0,  # Parse and create context
    ORD_PARSE_VARIABLES              = 0xAAF1,  # Parse and create variables instances
    ORD_PARSE_VARIABLES_S_DATA       = 0xAAFA,  # Fill created scalar variables with data
    ORD_PARSE_VARIABLES_ARR_DATA     = 0xAAFB
    ORD_PARSE_LOOP_CFG               = 0xAAA0,  # Create loop with provided config
    ORD_PARSE_CODE_CFG               = 0xAAAA,  # Create code context with provided config (block list)
    ORD_PARSE_ACCESS_CFG             = 0xAAAB,  # Create access instances storage
    ORD_PARSE_BLOCK_HEADER           = 0xAAB0
    ORD_PARSE_BLOCK_INPUTS           = 0xAAB1
    ORD_PARSE_BLOCK_OUTPUTS          = 0xAAB2
    ORD_PARSE_BLOCK_DATA             = 0xAABA
    ORD_PARSE_RESET_STATUS           = 0xAA00,  # Reset parser status to initial state (for new code parsing)
    ORD_PARSE_SUBSCRIPTION_INIT      = 0xAAC0,  # Initialize subscription system with provided config
    ORD_PARSE_SUBSCRIPTION_ADD       = 0xAAC1,  # Add subscription
    ORD_RESET_ALL                    = 0x0001,  # Brings emulator to startup state, provides way to eaisly send new code
    ORD_RESET_BLOCKS                 = 0x0002,  # Reset all blocks and theirs data
    ORD_RESET_MGS_BUF                = 0x0003,  # Clear msg buffer
    ORD_EMU_LOOP_START               = 0x1000,  # start loop / resume
    ORD_EMU_LOOP_STOP                = 0x2000,  # stop loop aka pause
    ORD_EMU_LOOP_INIT                = 0x3000,  # init loop first time
    ORD_EMU_INIT_WITH_DBG            = 0x3333,  # init loop with debug
    ORD_EMU_SET_PERIOD               = 0x4000,  # change period of loop
    ORD_EMU_RUN_ONCE                 = 0x5000,  # Run one cycle and wait for next order
    ORD_EMU_RUN_WITH_DEBUG           = 0x6000,  # Run with debug (Dump after each cycle)
    ORD_EMU_RUN_ONE_STEP             = 0x7000,  # Run one block / one step (With debug)



class block_types_t(IntEnum):
    """Block type identification code"""
    BLOCK_MATH                       = 0x01
    BLOCK_SET                        = 0x02
    BLOCK_LOGIC                      = 0x03
    BLOCK_COUNTER                    = 0x04
    BLOCK_CLOCK                      = 0x05
    BLOCK_FOR                        = 0x08
    BLOCK_TIMER                      = 0x09
    BLOCK_IN_SELECTOR                = 0x0A
    BLOCK_Q_SELECTOR                 = 0x0B
    BLOCK_LATCH                      = 0x0C



class block_packet_id_t(IntEnum):
    """Block data packet IDs - identifies type of data in PACKET_H_BLOCK_DATA packets"""
    PKT_CONSTANTS                    = 0x00,  # Constants: [cnt:u8][float...]
    PKT_CFG                          = 0x01,  # Configuration: block-specific config data
    PKT_INSTRUCTIONS                 = 0x10,  # Instructions: bytecode for Math, Logic blocks
    PKT_OPTIONS_BASE                 = 0x20,  # Selector options: 0x20 + option_index


class emu_err_t(IntEnum):
    EMU_OK                          = 0,
    EMU_ERR_INVALID_STATE           = 0xE001,
    EMU_ERR_INVALID_ARG             = 0xE002,
    EMU_ERR_INVALID_DATA            = 0xE003,
    EMU_ERR_PACKET_EMPTY            = 0xE004,
    EMU_ERR_PACKET_INCOMPLETE       = 0xE005, 
    EMU_ERR_PACKET_NOT_FOUND        = 0xE006,
    EMU_ERR_PARSE_INVALID_REQUEST   = 0xE007,
    EMU_ERR_DENY                    = 0xE008,
    EMU_ERR_ORD_FAILED              = 0xE009,
    EMU_ERR_ORD_DENY                = 0xE00A,
    EMU_ERR_ORD_CANNOT_EXECUTE      = 0xE00B,
    EMU_ERR_UNLIKELY                = 0xEFFF,
    EMU_ERR_NO_MEM                  = 0xF000, 
    EMU_ERR_MEM_ALLOC               = 0xF001, 
    EMU_ERR_MEM_ACCESS_ALLOC        = 0xF002, 
    EMU_ERR_MEM_INVALID_REF_ID      = 0xF003, 
    EMU_ERR_MEM_INVALID_IDX         = 0xF004, 
    EMU_ERR_MEM_OUT_OF_BOUNDS       = 0xF005, 
    EMU_ERR_MEM_INVALID_DATATYPE    = 0xF006,
    EMU_ERR_NULL_PTR                = 0xF007, 
    EMU_ERR_NULL_PTR_ACCESS         = 0xF008, 
    EMU_ERR_NULL_PTR_INSTANCE       = 0xF009, 
    EMU_ERR_NULL_PTR_CONTEXT        = 0xF00A, 
    EMU_ERR_MEM_ALREADY_CREATED     = 0xF00B,
    EMU_ERR_BLOCK_Generic           = 0xB000,
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
    EMU_ERR_BLOCK_FAILED            = 0xB00B,
    EMU_ERR_WTD_TRIGGERED           = 0xA000,
    EMU_ERR_MEM_INVALID_ACCESS      = 0xA001,
    EMU_ERR_LOOP_NOT_INITIALIZED    = 0xA002,
    EMU_ERR_BLOCK_SELECTOR_OOB      = 0xA003,
    EMU_ERR_CTX_INVALID_ID          = 0xA004,
    EMU_ERR_CTX_ALREADY_CREATED     = 0xA005,
    EMU_ERR_INVALID_PACKET_SIZE     = 0xA006,
    EMU_ERR_SEQUENCE_VIOLATION      = 0xA007,
    EMU_ERR_SUBSCRIPTION_FULL       = 0xA008,

OWNER_NAMES = [
    "",
    "mem_free_contexts",
    "mem_alloc_context",
    "mem_parse_create_context",
    "mem_parse_create_scalar_instances",
    "mem_parse_create_array_instances",
    "mem_parse_scalar_data",
    "mem_parse_array_data",
    "mem_parse_context_data_packets",
    "mem_set",
    "access_system_init",
    "parse_manager",
    "parse_source_add",
    "parse_blocks_total_cnt",
    "parse_block",
    "parse_blocks_verify_all",
    "parse_block_inputs",
    "parse_block_outputs",
    "loop_init",
    "loop_start",
    "loop_stop",
    "loop_set_period",
    "loop_run_once",
    "loop_deinit",
    "execute_code",
    "if_execute_loop_start",
    "if_execute_loop_stop",
    "if_execute_loop_init",
    "block_timer",
    "block_timer_parse",
    "block_timer_verify",
    "block_set",
    "block_set_parse",
    "block_set_verify",
    "block_math_parse",
    "block_math",
    "block_math_verify",
    "block_for",
    "block_for_parse",
    "block_for_verify",
    "block_logic_parse",
    "block_logic",
    "block_logic_verify",
    "block_counter",
    "block_counter_parse",
    "block_counter_verify",
    "block_clock",
    "block_clock_parse",
    "block_clock_verify",
    "block_latch",
    "block_latch_parse",
    "block_latch_verify",
    "block_set_output",
    "mem_register_context",
    "_parse_scalar_data",
    "_parse_array_data",
    "mem_pool_access_scalar_create",
    "access_system_free",
    "mem_access_parse_node_recursive",
    "_resolve_mem_offset",
    "mem_get",
    "block_check_in_true",
    "block_in_selector",
    "block_q_selector",
    "mem_context_delete",
    "mem_allocate_context",
    "mem_access_allocate_space",
    "mem_parse_instance_packet",
    "mem_fill_instance_scalar",
    "mem_fill_instance_array",
    "mem_parse_access_create",
    "parse_cfg",
    "block_parse_input",
    "block_parse_output",
    "parse_block_data",
    "subscribe_parse_init",
    "subscribe_parse_register",
    "subscribe_process",
    "subscribe_reset",
    "subscribe_send",
]

LOG_NAMES = [
    "context_freed",
    "context_allocated",
    "scalars_created",
    "arrays_created",
    "data_parsed",
    "var_set",
    "access_sys_initialized",
    "loop_initialized",
    "loop_started",
    "loop_stopped",
    "period_changed",
    "loop_single_step",
    "interface_loop_init",
    "source_added",
    "execution_finished",
    "blocks_list_allocated",
    "blocks_parsed_partial",
    "blocks_parsed_all",
    "blocks_verified",
    "block_timer_executed",
    "block_timer_parsed",
    "block_timer_verified",
    "block_set_executed",
    "block_math_parsed",
    "block_math_executed",
    "block_math_verified",
    "block_for_executed",
    "block_for_parsed",
    "block_for_verified",
    "block_logic_parsed",
    "block_logic_executed",
    "block_logic_verified",
    "block_counter_idle",
    "block_counter_executed",
    "block_counter_parsed",
    "block_counter_verified",
    "block_clock_idle",
    "block_clock_executed",
    "block_clock_parsed",
    "block_clock_verified",
    "context_registered",
    "access_pool_allocated",
    "mem_set",
    "mem_access_parse_success",
    "loop_starting",
    "variables_allocated",
    "loop_ran_once",
    "loop_period_set",
    "resolving_access",
    "access_out_of_bounds",
    "mem_invalid_data_type",
    "mem_get_failed",
    "executing_block",
    "loop_reinitialized",
    "loop_task_already_exists",
    "blocks_parsed_once",
    "parsed_block_inputs",
    "parsed_block_outputs",
    "block_inactive",
    "finished",
    "block_selector_executed",
    "block_selector_verified",
    "block_selector_freed",
    "block_selector_parsed",
    "CTX_DESTROYED",
    "ctx_created",
    "created_ctx",
    "clock_out_active",
    "clock_out_inactive",
    "to_large_to_sub",
]