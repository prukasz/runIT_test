#pragma once
#include "emulator_logging.h"
#include "mem_types.h"
#include "gatt_buff.h"

#define MAX_CONTEXTS 8
#define MAX_DIMS 3

/**
 * @brief Global context structs, storage for all created contexts 
 * @note context can be accesed directly from here, so every data pools can be accesed
 */
extern mem_context_t mem_contexts[MAX_CONTEXTS];

/**
 * @brief Parse and create context from provided packet
 * @param data packet buff (skip header)
 * @param data_len packet buff len (-header len)
 * @param nothing pass NULL
 * @return emu_result_t.code = EMU_OK when success, else look emu_result_t struct def
 * @note Packet [uint8_t context id] TYPES_CNT(7)*([uint32_t heap_elements]+[uint16_t max_instances]+[uint16_t max_dims])
 */
emu_result_t emu_mem_parse_create_context(const uint8_t *data,const uint16_t data_len, void *nothing);


/**
 * @brief Parse and create instances 
 * @param data packet buff (skip header)
 * @param data_len packet buff len (-header len)
 * @param nothing pass NULL
 * @return emu_result_t.code = EMU_OK when success, else look at emu_result_t struct def
 * @note This will only succed when context is already created
 */
emu_result_t emu_mem_parse_instance_packet(const uint8_t *data, const uint16_t data_len, void* nothing);

/**
 * @brief Parse and fill scalar instances (dims == 0) with provided data 
 * @param data packet buff (skip header)
 * @param data_len packet buff len (-header len)
 * @param nothing pass NULL
 * @return emu_result_t.code = EMU_OK when success, else look at emu_result_t struct def
 * @note This will only succed when context and isntance already created
 * @note Packet [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + count*([uint16_t inst_idx]+[sizeof(type) data])
 */
emu_result_t emu_mem_fill_instance_scalar(const uint8_t* data, const uint16_t data_len, void* nothing);

/**
 * @brief Parse and fill arrays instances
 * @param data packet buff (skip header)
 * @param data_len packet buff len (-header len)
 * @param nothing pass NULL
 * @return emu_result_t.code = EMU_OK when success, else look at emu_result_t struct def
 * @note This will only succed when context and isntance already created, make sure to provide data with correct lenght 
 * @note Packet [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + | count*([uint16_t instance_idx] + [uint16_t start_idx] + [uint16_t items] + items* [sizeof(type) data]] |
 */
emu_result_t emu_mem_fill_instance_array(const uint8_t* data, const uint16_t data_len, void* nothing);


/**
 * @brief Alternate runtime version, avoid usage as there is almost no error checking
 * @param data make sure that data is complete 
 * @note This version may not be complete / optimised
 */
emu_err_t emu_mem_fill_instance_scalar_fast(const uint8_t* data);

/**
 * @brief Alternate runtime version, avoid usage as there is almost no error checking
 * @param data make sure that data is complete 
 * @note This version may not be complete / optimised
 */
emu_err_t emu_mem_fill_instance_array_fast(const uint8_t* data);



