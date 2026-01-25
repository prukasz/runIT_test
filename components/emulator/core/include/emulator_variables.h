#pragma once
#include "emulator_logging.h"
#include "mem_types.h"
#include "gatt_buff.h"

#define MAX_CONTEXTS 8
#define MAX_DIMS 3

extern mem_context_t mem_contexts[MAX_CONTEXTS];

emu_result_t emu_mem_parse_create_context(const uint8_t *data,const uint16_t data_len);

emu_result_t emu_mem_fill_instance_scalar(const uint8_t* data, const uint16_t data_len);
emu_result_t emu_mem_fill_instance_array(const uint8_t* data, const uint16_t data_len);

emu_err_t emu_mem_fill_instance_scalar_fast(const uint8_t* data);
emu_err_t emu_mem_fill_instance_array_fast(const uint8_t* data);



