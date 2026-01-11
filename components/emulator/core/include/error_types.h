#pragma once 
#include <stdint.h>
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
    EMU_ERR_MEM_INVALID_ACCESS

} emu_err_t;

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
    EMU_OWNER_emu_block_set_output,
    EMU_OWNER_emu_mem_register_context,
    EMU_OWNER__parse_scalar_data,
    EMU_OWNER__parse_array_data,
    EMU_OWNER_mem_pool_access_scalar_create,
    EMU_OWNER_emu_access_system_free,
    EMU_OWNER_mem_access_parse_node_recursive,
    EMU_OWNER__resolve_mem_offset,
    EMU_OWNER_mem_get,

}emu_owner_t;

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
} emu_log_t;


typedef struct{
    emu_log_t log;
    emu_owner_t owner;
    uint16_t owner_idx;
    uint64_t time;
    uint64_t cycle;
} emu_report_t;

typedef struct {
    emu_err_t   code;  
    uint16_t    owner; 
    uint16_t    owner_idx;
    uint8_t     abort   : 1;
    uint8_t     warning : 1; 
    uint8_t     notice  : 1;
    uint8_t     depth   : 5; 
    uint64_t    time;
    uint64_t    cycle;
} emu_result_t;
