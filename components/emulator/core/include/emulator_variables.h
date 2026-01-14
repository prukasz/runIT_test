#pragma once
#include "emulator_logging.h"
#include "mem_types.h"
#include "gatt_buff.h"


/**************************************************************************************************************************************
 * Emulator memory contexts management is responsible for handling multiple isolated memory spaces for variables instances within the emulator.
 * Each context can hold its own set of variables, allowing for modular and independent execution environments.
 * Access to variables is managed via context IDs, ensuring that operations on variables are correctly scoped to their respective contexts.
 * 
 * This header provides functions to create, allocate, parse, and free memory contexts.
 * For memory access functions see emulator_variables_acces.h
 * 
 * Instances are created based on parsed definitions, and data packets can be filled into these instances.
 * 
 * In memory we have: Scalars and Arrays. Scalars are single value variables, while Arrays can hold multiple values and have dimensions (upto 4D).
 * Each type has its own data pool and instance management.
 * 
 * Types supported are defined in emulator_types.h and include UINT8, UINT16, UINT32, INT8, INT16, INT32, FLOAT, DOUBLE, BOOL.
 * As for now bool is stored as uint8_t.
 * 
 * There are 2 types of instances: scalar instances and array instances each represented by their own struct.
 * Array structure is in reality an scalar struct with extra dimension sizes appended after it.
 * To differentiate between scalar and array instance we use dims_cnt field. If dims_cnt is 0 then its scalar else its array.)
 * 
 * see mem_types.h for more info
 * 
 **************************************************************************************************************************************/

#define MAX_MEM_CONTEXTS 8

/**
 * @brief Reset all memory contexts
 */
void emu_mem_free_contexts();


/**
 * @brief Create memory context
 * @param context_id under what id
 * @param inst_counts list of total instances for each type (scalars + arrays)
 * @param var_counts total size of data pool to be created in elements 
 * @param total_extra_dims total dimenstions count for each type, used to create arens for instances (cnt += dims_cnt of every array of given type)
 */
emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                            uint16_t var_counts[TYPES_COUNT], 
                            uint16_t inst_counts[TYPES_COUNT], 
                            uint16_t total_extra_dims[TYPES_COUNT]) ;


/**
* @brief Parse for sizes for emu_mem_alloc_context() and create context
*/     
emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source);

/**
* @brief Scan for provided instances definitions
*/  
emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source);

/**
* @brief Scan for provided instances definitions size idx type etc
*/  
emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source);


/**
* @brief This will scan for packets for given context and then fill created variables with provided values
*/                            
emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem);


