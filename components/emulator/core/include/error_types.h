#pragma once 
#include <stdint.h>


/**
 * @brief Emulator error codes used throughout the emulator core
 * @note Please do not change existing error codes as they are used in logging and error reporting
 * New error codes should have consistent naming scheme
 */
typedef enum {
    EMU_OK = 0,

    /* --- EXECUTION / ORDER / PARSING ERRORS (0xE...) --- */
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

    /* --- MEMORY ERRORS (0xF...) --- */
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

    /* --- BLOCK SPECIFIC ERRORS (0xB...) --- */
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

    /* --- SYSTEM WATCHDOG (0xA...) --- */
    EMU_ERR_WTD_TRIGGERED           = 0xA000,
    EMU_ERR_MEM_INVALID_ACCESS, 
    EMU_ERR_LOOP_NOT_INITIALIZED,
    EMU_ERR_BLOCK_SELECTOR_OOB,
    EMU_ERR_CTX_INVALID_ID,
    EMU_ERR_CTX_ALREADY_CREATED,
    EMU_ERR_INVALID_PACKET_SIZE,
    EMU_ERR_SEQUENCE_VIOLATION,


} emu_err_t;


/**
 * @brief Enum of error owners for tracking error sources
 * @note Please do not change existing owner codes as they are used in logging and error reporting
 * New owner codes should have consistent naming scheme example: EMU_OWNER_<function_name> 
 */
typedef enum {
    EMU_OWNER_emu_mem_free_contexts = 1,
    EMU_OWNER_emu_mem_alloc_context,
    EMU_OWNER_emu_mem_parse_create_context,
    EMU_OWNER_emu_mem_parse_create_scalar_instances,
    EMU_OWNER_emu_mem_parse_create_array_instances,
    EMU_OWNER_emu_mem_parse_scalar_data,
    EMU_OWNER_emu_mem_parse_array_data,
    EMU_OWNER_emu_mem_parse_context_data_packets,
    EMU_OWNER_mem_set,
    EMU_OWNER_emu_access_system_init,
    EMU_OWNER_emu_parse_manager,
    EMU_OWNER_emu_parse_source_add,
    EMU_OWNER_emu_parse_blocks_total_cnt,
    EMU_OWNER_emu_parse_block,
    EMU_OWNER_emu_parse_blocks_verify_all,
    EMU_OWNER_emu_parse_block_inputs,
    EMU_OWNER_emu_parse_block_outputs,
    EMU_OWNER_emu_loop_init,
    EMU_OWNER_emu_loop_start,
    EMU_OWNER_emu_loop_stop,
    EMU_OWNER_emu_loop_set_period,
    EMU_OWNER_emu_loop_run_once,
    EMU_OWNER_emu_loop_deinit,
    EMU_OWNER_emu_execute_code,
    EMU_OWNER_interface_execute_loop_start_execution,
    EMU_OWNER_interface_execute_loop_stop_execution,
    EMU_OWNER_interface_execute_loop_init,
    EMU_OWNER_block_timer,
    EMU_OWNER_block_timer_parse,
    EMU_OWNER_block_timer_verify,
    EMU_OWNER_block_set,
    EMU_OWNER_block_math_parse,
    EMU_OWNER_block_math,
    EMU_OWNER_block_math_verify,
    EMU_OWNER_block_for,
    EMU_OWNER_block_for_parse,
    EMU_OWNER_block_for_verify,
    EMU_OWNER_block_logic_parse,
    EMU_OWNER_block_logic,
    EMU_OWNER_block_logic_verify,
    EMU_OWNER_block_counter,
    EMU_OWNER_block_counter_parse,
    EMU_OWNER_block_counter_verify,
    EMU_OWNER_block_clock,
    EMU_OWNER_block_clock_parse,
    EMU_OWNER_block_clock_verify,
    EMU_OWNER_block_set_output,
    EMU_OWNER_emu_mem_register_context,
    EMU_OWNER__parse_scalar_data,
    EMU_OWNER__parse_array_data,
    EMU_OWNER_mem_pool_access_scalar_create,
    EMU_OWNER_emu_access_system_free,
    EMU_OWNER_mem_access_parse_node_recursive,
    EMU_OWNER__resolve_mem_offset,
    EMU_OWNER_mem_get,
    EMU_OWNER_block_check_EN,
    EMU_OWNER_block_selector,
    EMU_OWNER_block_selector_parse,
    EMU_OWNER_block_selector_verify,
    EMU_OWNER_block_selector_free,
    EMU_OWNER_mem_context_delete,
    EMU_OWNER_mem_allocate_context,
    EMU_OWNER_mem_access_allocate_space,

    EMU_OWNER_mem_parse_instance_packet,
    EMU_OWNER_emu_mem_fill_instance_scalar,
    EMU_OWNER_emu_mem_fill_instance_array,
    EMU_OWNER_emu_mem_parse_access_create,
    EMU_OWNER_parse_cfg,
    EMU_OWNER_emu_block_parse_input,
    EMU_OWNER_emu_block_parse_output,
    EMU_OWNER_parse_block_data,

}emu_owner_t;

/** 
* @brief Emulator logging messages
* Use names EMU_LOG_<description>
* descriptions are treated as semi strings (so please use underscores _ to separate words)
* @note Please do not change existing log names as they are used in logging and error reporting
 */
typedef enum {
    EMU_LOG_context_freed,
    EMU_LOG_context_allocated,
    EMU_LOG_scalars_created,
    EMU_LOG_arrays_created,
    EMU_LOG_data_parsed,
    EMU_LOG_var_set,
    EMU_LOG_access_sys_initialized,
    EMU_LOG_loop_initialized,
    EMU_LOG_loop_started,
    EMU_LOG_loop_stopped,
    EMU_LOG_period_changed,
    EMU_LOG_loop_single_step,
    EMU_LOG_interface_loop_init,
    EMU_LOG_source_added,
    EMU_LOG_execution_finished,
    EMU_LOG_blocks_list_allocated,
    EMU_LOG_blocks_parsed_partial,
    EMU_LOG_blocks_parsed_all,
    EMU_LOG_blocks_verified,
    EMU_LOG_block_timer_executed,
    EMU_LOG_block_timer_parsed,
    EMU_LOG_block_timer_verified,
    EMU_LOG_block_set_executed,
    EMU_LOG_block_math_parsed,
    EMU_LOG_block_math_executed,
    EMU_LOG_block_math_verified,
    EMU_LOG_block_for_executed,
    EMU_LOG_block_for_parsed,
    EMU_LOG_block_for_verified,
    EMU_LOG_block_logic_parsed,
    EMU_LOG_block_logic_executed,
    EMU_LOG_block_logic_verified,
    EMU_LOG_block_counter_idle,
    EMU_LOG_block_counter_executed,
    EMU_LOG_block_counter_parsed,
    EMU_LOG_block_counter_verified,
    EMU_LOG_block_clock_idle,
    EMU_LOG_block_clock_executed,
    EMU_LOG_block_clock_parsed,
    EMU_LOG_block_clock_verified,
    EMU_LOG_context_registered,
    EMU_LOG_access_pool_allocated,
    EMU_LOG_mem_set,
    EMU_LOG_mem_access_parse_success,
    EMU_LOG_loop_starting,
    EMU_LOG_variables_allocated,
    EMU_LOG_loop_ran_once,
    EMU_LOG_loop_period_set,
    EMU_LOG_resolving_access,
    EMU_LOG_access_out_of_bounds, 
    EMU_LOG_mem_invalid_data_type,
    EMU_LOG_mem_get_failed,
    EMU_LOG_executing_block,
    EMU_LOG_loop_reinitialized,
    EMU_LOG_loop_task_already_exists,
    EMU_LOG_blocks_parsed_once,
    EMU_LOG_parsed_block_inputs,
    EMU_LOG_parsed_block_outputs,
    EMU_LOG_block_inactive,
    EMU_LOG_finished,
    EMU_LOG_block_selector_executed,
    EMU_LOG_block_selector_verified,
    EMU_LOG_block_selector_freed,
    EMU_LOG_block_selector_parsed,
    EMU_LOG_CTX_DESTROYED,
    EMU_LOG_ctx_created,
    EMU_LOG_created_ctx,
    EMU_LOG_clock_out_active,
    EMU_LOG_clock_out_inactive,
} emu_log_t;


/**
 * @brief Emulator report structure for logging non-critical information
 */
typedef struct{
    /**
     * @brief Log message (from emu_log_t enum)
     * usually we need to create new one for each new log message
     */
    emu_log_t log;
    /**
     * @brief Owner of the log (from emu_owner_t enum) 
     * usually function where log is created
     */
    emu_owner_t owner;
    /**
     * @brief Owner index (usually index of block if possible, else 0/0xFFFF)
     */
    uint16_t owner_idx;
    /**
     * @brief Time in ms when log was created (loop time context)
     * @note This is set automatically during logging
     */
    uint64_t time;
    /**
     * @brief Cycle count when log was created (loop iteration context)
     * @note This is set automatically during logging
     */
    uint64_t cycle;
} emu_report_t;

/**
 * @brief Emulator result structure used for error reporting and function results
 */
typedef struct {
    /**
     * @brief Error code (emu_err_t) from operation (EMU_OK if no error)
     */
    emu_err_t   code;  
    /**
     * @brief Owner of the error (from emu_owner_t enum) 
     * usually function where error is created
     */
    uint16_t    owner; 
    /**
     * @brief Owner index (usually index of block if possible, else 0/0xFFFF)
     */
    uint16_t    owner_idx;
    /**
     * @brief Abort flag indicating if operation should abort further processing
     */
    uint8_t     abort   : 1;
    /**
     * @brief Warning flag indicating if error is just a warning
     */

    uint8_t     warning : 1; 
    /**
     * @brief Notice flag indicating if error is just a notice (aka can be ignored)
     */
    uint8_t     notice  : 1;
        /**
     * @brief Depth of the error in call stack (0 for top level)
     */
    uint8_t     depth   : 5; 
    /**
     * @brief Time in ms when error was created (loop time context)
     * @note This is set automatically during error reporting
     */
    uint64_t    time;
    /**
     * @brief Cycle count when error was created (loop iteration context)
     * @note This is set automatically during error reporting
     */
    uint64_t    cycle;
} emu_result_t;
