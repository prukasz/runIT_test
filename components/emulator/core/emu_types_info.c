#include "emu_types_info.h"
#include <stdbool.h>

const uint8_t MEM_TYPE_SIZES[DATA_TYPES_CNT] = {
    sizeof(uint8_t),  // UI8
    sizeof(uint16_t), // UI16
    sizeof(uint32_t), // UI32
    sizeof(int16_t),  // I16
    sizeof(int32_t),  // I32
    sizeof(float),    // F
    sizeof(bool)      // B
};

const char *EMU_DATATYPE_TO_STR[7] = {
    "MEM_U8",
    "MEM_U16",
    "MEM_U32",
    "MEM_I16",
    "MEM_I32",
    "MEM_F",
    "MEM_B"
};

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

const char* EMU_OWNER_TO_STR(emu_owner_t owner) {
    switch (owner) {
        case EMU_OWNER_emu_mem_free_contexts: return "emu_mem_free_contexts";
        case EMU_OWNER_emu_mem_alloc_context: return "emu_mem_alloc_context";
        case EMU_OWNER_emu_mem_parse_create_context: return "emu_mem_parse_create_context";
        case EMU_OWNER_emu_mem_parse_create_scalar_instances: return "emu_mem_parse_create_scalar_instances";
        case EMU_OWNER_emu_mem_parse_create_array_instances: return "emu_mem_parse_create_array_instances";
        case EMU_OWNER_emu_mem_parse_scalar_data: return "emu_mem_parse_scalar_data";
        case EMU_OWNER_emu_mem_parse_array_data: return "emu_mem_parse_array_data";
        case EMU_OWNER_emu_mem_parse_context_data_packets: return "emu_mem_parse_context_data_packets";
        case EMU_OWNER_mem_set: return "mem_set";
        case EMU_OWNER_emu_access_system_init: return "emu_access_system_init";
        case EMU_OWNER_emu_parse_manager: return "emu_parse_manager";
        case EMU_OWNER_emu_parse_source_add: return "emu_parse_source_add";
        case EMU_OWNER_emu_parse_blocks_total_cnt: return "emu_parse_blocks_total_cnt";
        case EMU_OWNER_emu_parse_block: return "emu_parse_block";
        case EMU_OWNER_emu_parse_blocks_verify_all: return "emu_parse_blocks_verify_all";
        case EMU_OWNER_emu_parse_block_inputs: return "emu_parse_block_inputs";
        case EMU_OWNER_emu_parse_block_outputs: return "emu_parse_block_outputs";
        case EMU_OWNER_emu_loop_init: return "emu_loop_init";
        case EMU_OWNER_emu_loop_start: return "emu_loop_start";
        case EMU_OWNER_emu_loop_stop: return "emu_loop_stop";
        case EMU_OWNER_emu_loop_set_period: return "emu_loop_set_period";
        case EMU_OWNER_emu_loop_run_once: return "emu_loop_run_once";
        case EMU_OWNER_emu_loop_deinit: return "emu_loop_deinit";
        case EMU_OWNER_emu_execute_code: return "emu_execute_code";
        case EMU_OWNER_interface_execute_loop_start_execution: return "interface_execute_loop_start_execution";
        case EMU_OWNER_interface_execute_loop_stop_execution: return "interface_execute_loop_stop_execution";
        case EMU_OWNER_interface_execute_loop_init: return "interface_execute_loop_init";
        case EMU_OWNER_block_timer: return "block_timer";
        case EMU_OWNER_block_timer_parse: return "block_timer_parse";
        case EMU_OWNER_block_timer_verify: return "block_timer_verify";
        case EMU_OWNER_block_set: return "block_set";
        case EMU_OWNER_block_math_parse: return "block_math_parse";
        case EMU_OWNER_block_math: return "block_math";
        case EMU_OWNER_block_math_verify: return "block_math_verify";
        case EMU_OWNER_block_for: return "block_for";
        case EMU_OWNER_block_for_parse: return "block_for_parse";
        case EMU_OWNER_block_for_verify: return "block_for_verify";
        case EMU_OWNER_block_logic_parse: return "block_logic_parse";
        case EMU_OWNER_block_logic: return "block_logic";
        case EMU_OWNER_block_logic_verify: return "block_logic_verify";
        case EMU_OWNER_block_counter: return "block_counter";
        case EMU_OWNER_block_counter_parse: return "block_counter_parse";
        case EMU_OWNER_block_counter_verify: return "block_counter_verify";
        case EMU_OWNER_block_clock: return "block_clock";
        case EMU_OWNER_block_clock_parse: return "block_clock_parse";
        case EMU_OWNER_block_clock_verify: return "block_clock_verify";
        case EMU_OWNER_block_set_output: return "block_set_output";
        case EMU_OWNER_emu_mem_register_context: return "emu_mem_register_context";
        case EMU_OWNER__parse_scalar_data: return "_parse_scalar_data";
        case EMU_OWNER__parse_array_data: return "_parse_array_data";
        case EMU_OWNER_mem_pool_access_scalar_create: return "mem_pool_access_scalar_create";
        case EMU_OWNER_emu_access_system_free: return "emu_access_system_free";
        case EMU_OWNER_mem_access_parse_node_recursive: return "mem_access_parse_node_recursive";
        case EMU_OWNER__resolve_mem_offset: return "_resolve_mem_offset";
        case EMU_OWNER_mem_get: return "mem_get";
        case EMU_OWNER_block_check_EN: return "block_check_in_true";
        default: return "UNKNOWN_OWNER";
    }
}

const char* EMU_LOG_TO_STR(emu_log_t log) {
    switch (log) {
        case EMU_LOG_context_freed: return "context_freed";
        case EMU_LOG_context_allocated: return "context_allocated";
        case EMU_LOG_scalars_created: return "scalars_created";
        case EMU_LOG_arrays_created: return "arrays_created";
        case EMU_LOG_data_parsed: return "data_parsed";
        case EMU_LOG_var_set: return "var_set";
        case EMU_LOG_access_sys_initialized: return "access_sys_initialized";
        case EMU_LOG_loop_initialized: return "loop_initialized";
        case EMU_LOG_loop_started: return "loop_started";
        case EMU_LOG_loop_stopped: return "loop_stopped";
        case EMU_LOG_period_changed: return "period_changed";
        case EMU_LOG_loop_single_step: return "loop_single_step";
        case EMU_LOG_interface_loop_init: return "interface_loop_init";
        case EMU_LOG_source_added: return "source_added";
        case EMU_LOG_execution_finished: return "execution_finished";
        case EMU_LOG_blocks_list_allocated: return "blocks_list_allocated";
        case EMU_LOG_blocks_parsed_partial: return "blocks_parsed_partial";
        case EMU_LOG_blocks_parsed_all: return "blocks_parsed_all";
        case EMU_LOG_blocks_verified: return "blocks_verified";
        case EMU_LOG_block_timer_executed: return "block_timer_executed";
        case EMU_LOG_block_timer_parsed: return "block_timer_parsed";
        case EMU_LOG_block_timer_verified: return "block_timer_verified";
        case EMU_LOG_block_set_executed: return "block_set_executed";
        case EMU_LOG_block_math_parsed: return "block_math_parsed";
        case EMU_LOG_block_math_executed: return "block_math_executed";
        case EMU_LOG_block_math_verified: return "block_math_verified";
        case EMU_LOG_block_for_executed: return "block_for_executed";
        case EMU_LOG_block_for_parsed: return "block_for_parsed";
        case EMU_LOG_block_for_verified: return "block_for_verified";
        case EMU_LOG_block_logic_parsed: return "block_logic_parsed";
        case EMU_LOG_block_logic_executed: return "block_logic_executed";
        case EMU_LOG_block_logic_verified: return "block_logic_verified";
        case EMU_LOG_block_counter_idle: return "block_counter_idle";
        case EMU_LOG_block_counter_executed: return "block_counter_executed";
        case EMU_LOG_block_counter_parsed: return "block_counter_parsed";
        case EMU_LOG_block_counter_verified: return "block_counter_verified";
        case EMU_LOG_block_clock_idle: return "block_clock_idle";
        case EMU_LOG_block_clock_executed: return "block_clock_executed";
        case EMU_LOG_block_clock_parsed: return "block_clock_parsed";
        case EMU_LOG_block_clock_verified: return "block_clock_verified";
        case EMU_LOG_context_registered: return "context_registered";
        case EMU_LOG_access_pool_allocated: return "access_pool_allocated";
        case EMU_LOG_mem_set: return "mem_set";
        case EMU_LOG_mem_access_parse_success: return "mem_access_parse_success";
        case EMU_LOG_loop_starting: return "loop_starting";
        case EMU_LOG_variables_allocated: return "variables_allocated";
        case EMU_LOG_loop_ran_once: return "loop_ran_once";
        case EMU_LOG_loop_period_set: return "loop_period_set";
        case EMU_LOG_resolving_access: return "resolving_access";
        case EMU_LOG_access_out_of_bounds: return "access_out_of_bounds";
        case EMU_LOG_mem_invalid_data_type: return "mem_invalid_data_type";
        case EMU_LOG_mem_get_failed: return "mem_get_failed";
        case EMU_LOG_executing_block: return "executing_block";
        case EMU_LOG_loop_reinitialized: return "loop_reinitialized";
        case EMU_LOG_loop_task_already_exists: return "loop_task_already_exists";
        case EMU_LOG_blocks_parsed_once: return "blocks_parsed_once";
        case EMU_LOG_parsed_block_inputs: return "parsed_block_inputs";
        case EMU_LOG_parsed_block_outputs: return "parsed_block_outputs";
        case EMU_LOG_block_inactive: return "block_inactive";
        case EMU_LOG_finished: return "finished";
        case EMU_LOG_clock_out_active: return "clock_out_active";
        case EMU_LOG_clock_out_inactive: return "clock_out_inactive";
        default: return "UNKNOWN_LOG";
    }
}

/**
 * @brief Return string representation of emu_order_t code
 */
const char* EMU_ORDER_TO_STR(emu_order_t order) {
    switch (order) {
        // --- PARSER ORDERS ---
        // case ORD_CREATE_CONTEXT:        return "ORD_CREATE_CONTEXT";
        // case ORD_PARSE_VARIABLES:       return "ORD_PARSE_VARIABLES";
        // case ORD_PARSE_VARIABLES_DATA:  return "ORD_PARSE_VARIABLES_DATA";
        // case ORD_EMU_CREATE_BLOCK_LIST: return "ORD_EMU_CREATE_BLOCK_LIST";
        // case ORD_EMU_CREATE_BLOCKS:     return "ORD_EMU_CREATE_BLOCKS";
        // case ORD_CHECK_CODE:            return "ORD_CHECK_CODE";

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


