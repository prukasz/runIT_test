#include "emulator_variables.h"
#include "emulator.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdarg.h>

static const char *TAG = "DATAHOLDER";


emu_err_t emulator_dataholder_create(emu_mem_t *mem, uint8_t *sizes)
{
    if (!mem || !sizes) {
        return EMU_ERR_INVALID_ARG;
    }
    size_t total_size = 0;
    total_size += sizes[0] * sizeof(int8_t);
    total_size += sizes[1] * sizeof(int16_t);
    total_size += sizes[2] * sizeof(int32_t);
    total_size += sizes[3] * sizeof(int64_t);
    total_size += sizes[4] * sizeof(uint8_t);
    total_size += sizes[5] * sizeof(uint16_t);
    total_size += sizes[6] * sizeof(uint32_t);
    total_size += sizes[7] * sizeof(uint64_t);
    total_size += sizes[8] * sizeof(float);
    total_size += sizes[9] * sizeof(double);
    total_size += sizes[10] * sizeof(bool);

    mem->_base_ptr = calloc(1, total_size);
    if (!mem->_base_ptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for dataholder (%d bytes)", total_size);
        return EMU_ERR_NO_MEMORY;
    }

    uint8_t* current_ptr = (uint8_t*)mem->_base_ptr;
    mem->i8  = (int8_t*)current_ptr;   current_ptr += sizes[0] * sizeof(int8_t);
    mem->i16 = (int16_t*)current_ptr;  current_ptr += sizes[1] * sizeof(int16_t);
    mem->i32 = (int32_t*)current_ptr;  current_ptr += sizes[2] * sizeof(int32_t);
    mem->i64 = (int64_t*)current_ptr;  current_ptr += sizes[3] * sizeof(int64_t);
    mem->u8  = (uint8_t*)current_ptr;  current_ptr += sizes[4] * sizeof(uint8_t);
    mem->u16 = (uint16_t*)current_ptr; current_ptr += sizes[5] * sizeof(uint16_t);
    mem->u32 = (uint32_t*)current_ptr; current_ptr += sizes[6] * sizeof(uint32_t);
    mem->u64 = (uint64_t*)current_ptr; current_ptr += sizes[7] * sizeof(uint64_t);
    mem->f   = (float*)current_ptr;    current_ptr += sizes[8] * sizeof(float);
    mem->d   = (double*)current_ptr;   current_ptr += sizes[9] * sizeof(double);
    mem->b   = (bool*)current_ptr;

    ESP_LOGI(TAG, "Dataholder created successfully");
    return EMU_OK;
}

void emulator_dataholder_free(emu_mem_t *mem)
{
    if (!mem || !mem->_base_ptr) return;

    // Free the simple 1D array memory
    free(mem->_base_ptr);

    // Free the parallel MD array memory
    if (mem->_md_base_ptr) {
        free(mem->_md_base_ptr);
    }
    if (mem->md_vars) {
        free(mem->md_vars);
    }

    *mem = (emu_mem_t){0}; // Clear the entire struct, including all pointers.
    ESP_LOGI(TAG, "Dataholder memory freed");
}

emu_err_t emulator_md_dataholder_create(emu_mem_t *mem, const emu_md_variable_desc_t* descriptors, size_t num_descriptors)
{
    if (!mem || !descriptors || num_descriptors == 0) {
        return EMU_ERR_INVALID_ARG;
    }

    // 1. Calculate total size and allocate the runtime variable array
    size_t total_size = 0;
    mem->md_vars = calloc(num_descriptors, sizeof(*mem->md_vars));
    if (!mem->md_vars) {
        ESP_LOGE(TAG, "Failed to allocate memory for MD variable descriptors");
        return EMU_ERR_NO_MEMORY;
    }
    mem->num_md_vars = num_descriptors;

    for (size_t i = 0; i < num_descriptors; ++i) {
        const emu_md_variable_desc_t* desc = &descriptors[i];
        size_t element_size = data_size(desc->type);
        size_t num_elements = 1;
        if (desc->num_dims >= 1) num_elements *= desc->dims[0];
        if (desc->num_dims >= 2) num_elements *= desc->dims[1];
        if (desc->num_dims >= 3) num_elements *= desc->dims[2];
        total_size += num_elements * element_size;
    }

    // 2. Allocate the single contiguous block for all MD data
    mem->_md_base_ptr = calloc(1, total_size > 0 ? total_size : 1);
    if (!mem->_md_base_ptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for MD dataholder (%d bytes)", total_size);
        free(mem->md_vars);
        mem->md_vars = NULL;
        mem->num_md_vars = 0;
        return EMU_ERR_NO_MEMORY;
    }

    // 3. Carve up the block and populate the runtime descriptors
    uint8_t* current_offset = (uint8_t*)mem->_md_base_ptr;
    for (size_t i = 0; i < num_descriptors; ++i) {
        const emu_md_variable_desc_t* desc = &descriptors[i];
        struct emu_md_variable* var = &mem->md_vars[i];

        *var = (struct emu_md_variable){
            .type = desc->type, .num_dims = desc->num_dims, .data = current_offset
        };
        memcpy(var->dims, desc->dims, sizeof(var->dims));

        size_t element_size = data_size(desc->type);
        size_t num_elements = 1;
        if (desc->num_dims >= 1) num_elements *= desc->dims[0];
        if (desc->num_dims >= 2) num_elements *= desc->dims[1];
        if (desc->num_dims >= 3) num_elements *= desc->dims[2];
        current_offset += num_elements * element_size;
    }

    ESP_LOGI(TAG, "MD Dataholder created successfully");
    return EMU_OK;
}

void* mem_get_md_element_ptr(const struct emu_md_variable* var, ...) {
    if (!var) return NULL;

    va_list args;
    va_start(args, var);

    size_t indices[3] = {0};
    for (uint8_t i = 0; i < var->num_dims; ++i) {
        indices[i] = va_arg(args, size_t);
        if (indices[i] >= var->dims[i]) { // Bounds check
            va_end(args);
            return NULL;
        }
    }
    va_end(args);

    // Calculate 1D index from MD indices (x, y, z)
    size_t index_1d = 0;
    switch (var->num_dims) {
        case 1:
            index_1d = indices[0];
            break;
        case 2:
            index_1d = indices[1] * var->dims[0] + indices[0]; // y * width + x
            break;
        case 3:
            index_1d = (indices[2] * var->dims[1] + indices[1]) * var->dims[0] + indices[0]; // (z * height + y) * width + x
            break;
    }
    return (void*)((uint8_t*)var->data + (index_1d * data_size(var->type)));
}
