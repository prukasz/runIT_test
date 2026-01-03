#include "emulator_variables.h"
#include "emulator_variables_acces.h"
#include "emulator_errors.h"
#include "gatt_buff.h"
#include "emulator_types.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EMU_VARIABLES";

/******************************************************
 * 
 * MEMORY CONTEXT
 * 
 *****************************************************/
#define MAX_MEM_CONTEXTS 8 
emu_mem_t* s_mem_contexts[MAX_MEM_CONTEXTS] = {NULL};

emu_mem_t* _get_mem_context(uint8_t id) {
    if (id >= MAX_MEM_CONTEXTS) return NULL;
    return s_mem_contexts[id];
}

void emu_mem_register_context(uint8_t id, emu_mem_t *mem_ptr) {
    if (id < MAX_MEM_CONTEXTS) {
        s_mem_contexts[id] = mem_ptr;
        LOG_I(TAG, "Registered Context ID %d", id);
        // }else{
        //     LOG_W(TAG, "Context %d already registered!", id);
        // }
    }
}

void emu_mem_free_context(emu_mem_t *mem) {
    if (!mem) return;

    void** pools_ptrs[] = {
        (void**)&mem->pool_ui8, (void**)&mem->pool_ui16, (void**)&mem->pool_ui32,
        (void**)&mem->pool_i8,  (void**)&mem->pool_i16,  (void**)&mem->pool_i32,
        (void**)&mem->pool_f,   (void**)&mem->pool_d,    (void**)&mem->pool_b
    };

    void*** instances_ptrs[] = {
        &mem->instances_ui8, &mem->instances_ui16, &mem->instances_ui32,
        &mem->instances_i8,  &mem->instances_i16,  &mem->instances_i32,
        &mem->instances_f,   &mem->instances_d,    &mem->instances_b
    };

    for (uint8_t i = 0; i < TYPES_COUNT; i++) {
        if (*pools_ptrs[i]) { free(*pools_ptrs[i]); *pools_ptrs[i] = NULL; }
        if (*instances_ptrs[i]) { free(*instances_ptrs[i]); *instances_ptrs[i] = NULL; }
        if (mem->arenas[i]) { free(mem->arenas[i]); mem->arenas[i] = NULL; }
        mem->instances_cnt[i] = 0;
    }
    LOG_I(TAG, "Memory context freed");
}

emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                                   uint16_t var_counts[TYPES_COUNT], 
                                   uint16_t inst_counts[TYPES_COUNT], 
                                   uint16_t total_dims[TYPES_COUNT]) 
{
    emu_result_t res = { .code = EMU_OK };
    
    emu_mem_t *mem;
    mem = calloc(1, sizeof(emu_mem_t));
    memset(mem, 0, sizeof(emu_mem_t));
    if (!mem) {
        LOG_E(TAG, "Context struct alloc failed for ID %d", context_id);
        return EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, 0);
    }
    emu_mem_register_context(context_id, mem);

    void** pools_ptrs[] = {
        (void**)&mem->pool_ui8, (void**)&mem->pool_ui16, (void**)&mem->pool_ui32,
        (void**)&mem->pool_i8,  (void**)&mem->pool_i16,  (void**)&mem->pool_i32,
        (void**)&mem->pool_f,   (void**)&mem->pool_d,    (void**)&mem->pool_b
    };
    void*** instances_ptrs[] = {
        &mem->instances_ui8, &mem->instances_ui16, &mem->instances_ui32,
        &mem->instances_i8,  &mem->instances_i16,  &mem->instances_i32,
        &mem->instances_f,   &mem->instances_d,    &mem->instances_b
    };

    for (int i = 0; i < TYPES_COUNT; i++) {
        //data pools 
        if (var_counts[i] > 0) {
            *pools_ptrs[i] = calloc(var_counts[i], DATA_TYPE_SIZES[i]);
            if (*pools_ptrs[i] == NULL) {
                LOG_E(TAG, "Ctx %d: Data Pool Alloc Failed Type %d (Cnt: %d)", context_id, i, var_counts[i]);
                res = EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, i);
                goto error;
            }
        }
        
        uint16_t cnt = inst_counts[i];
        mem->instances_cnt[i] = cnt;

        if (cnt > 0) {
            *instances_ptrs[i] = calloc(cnt, sizeof(void*));   
            if (*instances_ptrs[i] == NULL) {
                LOG_E(TAG, "Ctx %d: Inst Table Alloc Failed Type %d (Cnt: %d)", context_id, i, cnt);
                res = EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, i);
                goto error;
            }
            
            size_t arena_size = (cnt * sizeof(emu_mem_instance_s_t)) + total_dims[i];

            mem->arenas[i] = calloc(1, arena_size);
            if (mem->arenas[i] == NULL) {
                LOG_E(TAG, "Ctx %d: Arena Alloc Failed Type %d (%d bytes)", context_id, i, (int)arena_size);
                res = EMU_RESULT_CRITICAL(EMU_ERR_NO_MEM, i);
                goto error;
            }

            LOG_I(TAG, "Ctx %d Type %d: Allocated (Vars:%d, Inst:%d, Arena:%d B)", 
                  context_id, i, var_counts[i], cnt, (int)arena_size);
        }
    }

    LOG_I(TAG, "Context %d allocated successfully.", context_id);
    return res;

error:
    LOG_E(TAG, "Alloc context %d failed! Cleaning up.", context_id);
    emu_mem_free_context(mem);
    return res;
}

/*****************************************************************************************
 * 
 * PARSE + ALLOCATION (CONTEXT)
 * 
 *******************************************************************************************/

emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_SIZES) {
            uint8_t ctx_id = data[2]; 
            
            uint16_t idx = 3; 
            uint16_t var_counts[TYPES_COUNT];
            memcpy(var_counts, &data[idx], sizeof(var_counts)); idx += sizeof(var_counts);
            
            uint16_t inst_counts[TYPES_COUNT];
            memcpy(inst_counts, &data[idx], sizeof(inst_counts)); idx += sizeof(inst_counts);
            
            uint16_t extra_dims[TYPES_COUNT];
            memcpy(extra_dims, &data[idx], sizeof(extra_dims));
            
            for (int i = 0; i < TYPES_COUNT; i++) {
                LOG_I(TAG, "CTX_id %d, TYPE %s, instacnec cnt %d, total cnt %d", ctx_id, DATA_TYPE_TO_STR[i], inst_counts[i], var_counts[i]);
            }
            LOG_I(TAG, "Allocating context now");
            return emu_mem_alloc_context(ctx_id, var_counts, inst_counts, extra_dims);
        }
    }
    return EMU_RESULT_OK();
}

emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t packet_len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    const size_t EXPECTED_PAYLOAD = TYPES_COUNT * sizeof(uint16_t);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &packet_len);
        
        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_SCALAR_CNT) {
            uint8_t ctx_id = data[2];
            emu_mem_t *mem = _get_mem_context(ctx_id);
            if (!mem) {
                LOG_E(TAG, "Scalar Cnt: Ctx %d not found/allocated", ctx_id);
                return EMU_RESULT_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, i);   
            }
            
            uint16_t idx = 3; 
        
            void** instances_ptrs[] = {
                mem->instances_ui8, mem->instances_ui16, mem->instances_ui32,
                mem->instances_i8,  mem->instances_i16,  mem->instances_i32,
                mem->instances_f,   mem->instances_d,    mem->instances_b
            };

            for(uint8_t type = 0; type < TYPES_COUNT; type++) {
                uint16_t scalar_cnt = 0;
                memcpy(&scalar_cnt, &data[idx], sizeof(uint16_t));
                idx += sizeof(uint16_t);
                
                if (scalar_cnt == 0) continue;

                if (!mem->arenas[type] || !instances_ptrs[type]) {
                    LOG_E(TAG, "Scalar Cnt: Arena/Table missing for Type %s Ctx %d",DATA_TYPE_TO_STR[type], ctx_id);
                    return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, i);
                }

                uint8_t *arena_cursor = (uint8_t*)mem->arenas[type];
                void **instance_table = instances_ptrs[type];

                for (int k = 0; k < scalar_cnt; k++) {
                    instance_table[k] = arena_cursor;

                    emu_mem_instance_s_t *meta = (emu_mem_instance_s_t*)arena_cursor;
                    
                    meta->dims_cnt     = 0;    
                    meta->target_type  = type;       
                    meta->reference_id = ctx_id; 
                    meta->updated      = 1;      
                    meta->start_idx    = k;       
                    
                    arena_cursor += sizeof(emu_mem_instance_s_t);
                }
                
                mem->next_data_idx[type] = scalar_cnt;
                mem->next_instance_idx[type] = scalar_cnt;
                mem->arena_offset[type] = scalar_cnt * sizeof(emu_mem_instance_s_t);
                    
                LOG_I(TAG, "Ctx %d: Created %d SCALARS of Type %s", ctx_id, scalar_cnt, DATA_TYPE_TO_STR[type]);
            } 
            return EMU_RESULT_OK();
        }
    }
    return EMU_RESULT_WARN(EMU_ERR_PACKET_NOT_FOUND, VAR_H_SCALAR_CNT);
}

emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source) {
    uint8_t *data;       
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_ARR) {
            uint8_t ctx_id = data[2];
            emu_mem_t *mem = _get_mem_context(ctx_id);
            if (!mem) {
                LOG_E(TAG, "Ctx %d not found", ctx_id);
                return EMU_RESULT_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, i);
            }

            uint16_t idx = 3; 

            while (idx < len) {
                if ((idx + 2) > len) break;

                uint8_t dims_cnt    = data[idx++];
                uint8_t target_type = data[idx++];

                if ((idx + dims_cnt) > len) {
                    LOG_E(TAG, "Arr Def: Payload incomplete");
                    return EMU_RESULT_WARN(EMU_ERR_PACKET_INCOMPLETE, VAR_H_ARR);
                }

                void **instance_table = NULL;
                switch(target_type) {
                    case DATA_UI8: instance_table = mem->instances_ui8; break;
                    case DATA_UI16: instance_table = mem->instances_ui16; break;
                    case DATA_UI32: instance_table = mem->instances_ui32; break;
                    case DATA_I8:   instance_table = mem->instances_i8; break;
                    case DATA_I16:  instance_table = mem->instances_i16; break;
                    case DATA_I32:  instance_table = mem->instances_i32; break;
                    case DATA_F:    instance_table = mem->instances_f; break;
                    case DATA_D:    instance_table = mem->instances_d; break;
                    case DATA_B:    instance_table = mem->instances_b; break;
                    default: instance_table = NULL; break;
                }
                
                if (!instance_table) {
                    LOG_E(TAG, "Instance Table missing for Type %d", target_type);
                    return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, i);
                }

                uint32_t instance_idx = mem->next_instance_idx[target_type];
                uint32_t data_idx     = mem->next_data_idx[target_type];  
                uint8_t *arena_cursor = (uint8_t*)mem->arenas[target_type] + mem->arena_offset[target_type];

                if (instance_idx >= mem->instances_cnt[target_type]) {
                    LOG_E(TAG, "Max Instances Reached CTX %d Type %s",ctx_id, DATA_TYPE_TO_STR[target_type]);
                    return EMU_RESULT_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, i);
                }

                instance_table[instance_idx] = arena_cursor;

                emu_mem_instance_arr_t *meta = (emu_mem_instance_arr_t*)arena_cursor;
                meta->dims_cnt     = dims_cnt;
                meta->target_type  = target_type;
                meta->reference_id = ctx_id;
                meta->updated      = 1;
                meta->start_idx    = data_idx;

                uint32_t total_elements = 1;
                for (int d = 0; d < dims_cnt; d++) {
                    uint8_t dim_size = data[idx++];
                    meta->dim_sizes[d] = dim_size;
                    total_elements *= dim_size;
                }

                mem->next_data_idx[target_type]     += total_elements; 
                mem->next_instance_idx[target_type]++;
                mem->arena_offset[target_type]      += sizeof(emu_mem_instance_arr_t) + dims_cnt;

                LOG_I(TAG, "Ctx %d: Created ARRAY Type %d (Dims:%d, Elems:%ld)", 
                      ctx_id, target_type, dims_cnt, total_elements);
            }
            return EMU_RESULT_OK();
        }
    }
    return EMU_RESULT_WARN(EMU_ERR_PACKET_NOT_FOUND, VAR_H_ARR);
}

/*****************************************************************************
 * 
 * DATA PARSERS (INTO CONTEXT)
 * 
 *****************************************************************************/


 //eaisly get type and is scalar from packet ID
static int8_t _get_type_from_header(uint16_t header, bool *is_array) {
    *is_array = false;
    switch (header) {
        case VAR_H_DATA_S_UI8:  return DATA_UI8;
        case VAR_H_DATA_S_UI16: return DATA_UI16;
        case VAR_H_DATA_S_UI32: return DATA_UI32;
        case VAR_H_DATA_S_I8:   return DATA_I8;
        case VAR_H_DATA_S_I16:  return DATA_I16;
        case VAR_H_DATA_S_I32:  return DATA_I32;
        case VAR_H_DATA_S_F:    return DATA_F;
        case VAR_H_DATA_S_D:    return DATA_D;
        case VAR_H_DATA_S_B:    return DATA_B;

        case VAR_H_DATA_ARR_UI8:  *is_array = true; return DATA_UI8;
        case VAR_H_DATA_ARR_UI16: *is_array = true; return DATA_UI16;
        case VAR_H_DATA_ARR_UI32: *is_array = true; return DATA_UI32;
        case VAR_H_DATA_ARR_I8:   *is_array = true; return DATA_I8;
        case VAR_H_DATA_ARR_I16:  *is_array = true; return DATA_I16;
        case VAR_H_DATA_ARR_I32:  *is_array = true; return DATA_I32;
        case VAR_H_DATA_ARR_F:    *is_array = true; return DATA_F;
        case VAR_H_DATA_ARR_D:    *is_array = true; return DATA_D;
        case VAR_H_DATA_ARR_B:    *is_array = true; return DATA_B;
        
        default: return -1; 
    }
}

static void* _mem_get_pool_ptr(uint8_t context_id, uint8_t type) {
    emu_mem_t *mem = _get_mem_context(context_id);
    if (!mem) return NULL;
    switch (type) {
        case DATA_UI8:  return mem->pool_ui8;
        case DATA_UI16: return mem->pool_ui16;
        case DATA_UI32: return mem->pool_ui32;
        case DATA_I8:   return mem->pool_i8;
        case DATA_I16:  return mem->pool_i16;
        case DATA_I32:  return mem->pool_i32;
        case DATA_F:    return mem->pool_f;
        case DATA_D:    return mem->pool_d;
        case DATA_B:    return mem->pool_b;
        default:        return NULL;
    }
}

/**
 * @brief Internal, parse scalar data if detected
 */
static emu_err_t _parse_scalar_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id);
    if (!mem) {
        LOG_E(TAG, "Ctx %d not found", ctx_id);
        return EMU_ERR_MEM_INVALID_REF_ID;
    }
    void *pool_ptr = _mem_get_pool_ptr(ctx_id, type);
    if (!pool_ptr) {
        LOG_E(TAG, "Invalid pool ptr for type %s, ctx %d", DATA_TYPE_TO_STR[type], ctx_id);
        return EMU_ERR_NULL_PTR;}

    size_t cursor = 0;
    size_t elem_size = DATA_TYPE_SIZES[type];
    size_t entry_size = 2 + elem_size; 

    while (cursor + entry_size <= len) {
        uint16_t instance_idx = 0;
        memcpy(&instance_idx, &data[cursor], 2);
        cursor += 2;

        if (instance_idx >= mem->instances_cnt[type]) {
            LOG_E(TAG, "Data Parse: Instance Idx OOB (%d >= %d) for type %s", instance_idx, mem->instances_cnt[type], DATA_TYPE_TO_STR[type]);
            return EMU_ERR_MEM_OUT_OF_BOUNDS;
        }

        emu_mem_instance_iter_t meta = _mem_get_instance(mem, type, instance_idx);
        if (!meta.raw) {
            LOG_E(TAG, "Data Parse: Meta NULL for Inst %d", instance_idx);
            return EMU_ERR_MEM_OUT_OF_BOUNDS;}

        uint32_t pool_idx = meta.single->start_idx;
        uint8_t *dest = (uint8_t*)pool_ptr + (pool_idx * elem_size);
        
        memcpy(dest, &data[cursor], elem_size);
        cursor += elem_size;
    }
    
    LOG_I(TAG, "Ctx %d: Parsed Data Batch Type %s (Len %d)", ctx_id, DATA_TYPE_TO_STR[type], len);
    return EMU_OK;
}

/**
 * @brief Internal, parse array data if detected
 */
static emu_err_t _parse_array_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id);
    if (!mem) return EMU_ERR_MEM_INVALID_REF_ID;

    uint16_t instance_idx = 0;
    memcpy(&instance_idx, &data[0], 2);
    
    uint16_t offset_in_array = 0;
    memcpy(&offset_in_array, &data[2], 2);

    emu_mem_instance_iter_t meta = _mem_get_instance(mem, type, instance_idx);
    if (!meta.raw) {
        LOG_E(TAG, "NULL Inst %d", instance_idx);
        return EMU_ERR_MEM_OUT_OF_BOUNDS;
    }

    uint32_t pool_start_idx = meta.arr->start_idx;
    uint32_t final_pool_idx = pool_start_idx + offset_in_array;

    void *pool_ptr = _mem_get_pool_ptr(ctx_id, type);
    if (!pool_ptr) return EMU_ERR_NULL_PTR;

    size_t elem_size = DATA_TYPE_SIZES[type];
    size_t data_bytes = len - 4;
    
    uint8_t *dest = (uint8_t*)pool_ptr + (final_pool_idx * elem_size);
    memcpy(dest, &data[4], data_bytes);

    return EMU_OK;
}

emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem_ignored) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        uint16_t header = 0;
        memcpy(&header, data, 2); 

        bool is_array = false;
        int8_t type = _get_type_from_header(header, &is_array);

        if (type != -1) {
            uint8_t ctx_id = data[2];
            uint8_t *payload = &data[3];
            size_t payload_len = len - 3;

            emu_err_t err;
            if (is_array) {
                err = _parse_array_data(ctx_id, type, payload, payload_len);
            } else {
                err = _parse_scalar_data(ctx_id, type, payload, payload_len);
            }

            if (err != EMU_OK) {
                LOG_E(TAG, "Data Parse: Pkt %d Fail (Ctx:%d Type:%s Arr:%d) Err:%d", 
                      (int)i, ctx_id, DATA_TYPE_TO_STR[type], is_array, err);
                return EMU_RESULT_CRITICAL(err, i);
            } else {
                LOG_I(TAG, "Data Parse: Pkt %d OK (Ctx:%d Type:%s)", (int)i, ctx_id, DATA_TYPE_TO_STR[type]);
            }
        }
    }
    return EMU_RESULT_OK(); 
}