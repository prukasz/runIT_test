#pragma once
#include "emulator_logging.h"
#include "mem_types.h"
#include "gatt_buff.h"

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


