
#include "emulator_variables_acces.h"
#include "emulator_types.h"
#include "emulator_errors.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h> 

static const char *TAG_VARS = "EMU_VARS";
static const char *TAG_REF  = "EMU_REF";
static const char *TAG      = "EMU_MEM";


/***********************************************************************************************
 * 
 * REFERENCES POOL ALLOCATION ETC..
 * 
 ************************************************************************************************/

typedef struct {
    uint8_t *buffer;
    size_t   item_size;
    size_t   capacity;
    size_t   used_count;
    const char *tag;
} mem_pool_acces_s_t;

typedef struct {
    uint8_t *buffer;
    size_t   total_size;
    size_t   used_bytes;
    const char *tag;
} mem_pool_acces_arr_t;

static emu_err_t mem_pool_access_scalar_create(mem_pool_acces_s_t *pool, size_t item_size, size_t capacity, const char *tag) {
    pool->item_size = item_size;
    pool->capacity = capacity;
    pool->used_count = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(item_size * capacity);
    if (!pool->buffer) {
        LOG_E(TAG, "Scalar pool alloc failed for %s", tag);
        return EMU_ERR_NO_MEM;
    }
    memset(pool->buffer, 0, item_size * capacity);
    return EMU_OK;
}   

static void* mem_pool_acces_scalar_alloc(mem_pool_acces_s_t *pool) {
    if (pool->used_count >= pool->capacity) return NULL;
    void *ptr = pool->buffer + (pool->used_count * pool->item_size);
    pool->used_count++;
    return ptr;
}

static void mem_pool_acces_scalar_destroy(mem_pool_acces_s_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_count = 0;
}

static bool mem_pool_acces_arr_create(mem_pool_acces_arr_t *pool, size_t size, const char *tag) {
    pool->total_size = size;
    pool->used_bytes = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(size);
    if (!pool->buffer) {
        LOG_E(TAG, "Array pool alloc failed for %s", tag);
        return false;
    }
    memset(pool->buffer, 0, size);
    return true;
}

static void* mem_pool_acces_arr_alloc(mem_pool_acces_arr_t *pool, size_t size) {
    size_t aligned_size = (size + 3) & ~3;
    if (pool->used_bytes + aligned_size > pool->total_size) return NULL;
    void *ptr = pool->buffer + pool->used_bytes;
    pool->used_bytes += aligned_size;
    return ptr;
}

static void mem_pool_acces_arr_destroy(mem_pool_acces_arr_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_bytes = 0;
}

/* ============================================================================
    INTERNAL RESOLVERS
   ============================================================================ */

__attribute__((always_inline))static inline void* _mem_get_pool_ptr(uint8_t context_id, uint8_t type) {
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

__attribute__((always_inline)) inline emu_mem_instance_iter_t _mem_get_instance(emu_mem_t *mem, uint8_t type, uint16_t idx) {
    emu_mem_instance_iter_t iter = {0};
    if (!mem || idx >= mem->instances_cnt[type]) {
        return iter; 
    }
    switch (type) {
        case DATA_UI8:  iter.raw = mem->instances_ui8[idx]; break;
        case DATA_UI16: iter.raw = mem->instances_ui16[idx]; break;
        case DATA_UI32: iter.raw = mem->instances_ui32[idx]; break;
        case DATA_I8:   iter.raw = mem->instances_i8[idx]; break;
        case DATA_I16:  iter.raw = mem->instances_i16[idx]; break;
        case DATA_I32:  iter.raw = mem->instances_i32[idx]; break;
        case DATA_F:    iter.raw = mem->instances_f[idx]; break;
        case DATA_D:    iter.raw = mem->instances_d[idx]; break;
        case DATA_B:    iter.raw = mem->instances_b[idx]; break;
    }
    return iter;
}

__attribute((always_inline)) static inline emu_err_t _resolve_mem_offset(void *access_node, uint32_t *out_offset, uint8_t *out_type) {
    if (!access_node) return EMU_ERR_NULL_PTR;

    mem_acces_instance_iter_t access;
    access.raw = (uint8_t*)access_node;

    uint8_t  dims_cnt    = access.single->dims_cnt;
    uint8_t  target_type = access.single->target_type;
    uint16_t target_idx  = access.single->target_idx;

    *out_type = target_type;
    emu_mem_t *target_mem = _get_mem_context(access.single->reference_id);
    LOG_I(TAG, "Resolving acces to ctx %d, type %s, idx %d", access.single->reference_id, DATA_TYPE_TO_STR[target_type], target_idx);
    if (!target_mem) {
        LOG_E(TAG, "Invalid Ref ID %d", access.single->reference_id);
        return EMU_ERR_MEM_INVALID_REF_ID;
    }

    emu_mem_instance_iter_t meta = _mem_get_instance(target_mem, target_type, target_idx);
    if (!meta.raw) {
        LOG_E(TAG, "Null instance for Type:%s Idx:%d", DATA_TYPE_TO_STR[target_type], target_idx);
        return EMU_ERR_MEM_OUT_OF_BOUNDS;
    }

    if (dims_cnt == 0) {
        *out_offset = meta.single->start_idx;
        return EMU_OK;
    }

    mem_acces_arr_t        *acc_arr  = access.arr; 
    emu_mem_instance_arr_t *instance_arr = meta.arr;

    uint32_t final_offset = instance_arr->start_idx;
    uint32_t stride = 1;

    for (int8_t i = (int8_t)dims_cnt - 1; i >= 0; i--) {
        uint32_t index_val = 0;

        if (IDX_IS_DYNAMIC(acc_arr, i)) { 
            emu_variable_t idx_var = mem_get(acc_arr->indices[i].next_node, false);
            if (idx_var.error) return idx_var.error;
            index_val = (uint32_t)emu_var_to_double(idx_var);
        } 
        else {     
            index_val = acc_arr->indices[i].static_idx;
        }

        if (index_val >= instance_arr->dim_sizes[i]) {
            LOG_E(TAG, "Array Index OOB Dim %d: %ld >= %d", i, index_val, instance_arr->dim_sizes[i]);
            return EMU_ERR_MEM_OUT_OF_BOUNDS; 
        }

        final_offset += index_val * stride;
        stride *= instance_arr->dim_sizes[i];
    }

    *out_offset = final_offset;
    return EMU_OK;
}

/* ============================================================================
    MEM GET / SET / CONVERT
   ============================================================================ */

emu_mem_instance_iter_t mem_get_instance(void *global_access_node) {
    emu_mem_instance_iter_t iter = {0};
    if (!global_access_node) return iter; 
    mem_acces_s_t *header = (mem_acces_s_t*)global_access_node;
    emu_mem_t *mem = _get_mem_context(header->reference_id);
    if (!mem) return iter;

    return _mem_get_instance(mem, header->target_type, header->target_idx);
}
extern emu_mem_t* s_mem_contexts[];

emu_variable_t mem_get(void *global_access_node, bool by_reference) {
    emu_variable_t res = {0};
    
    if (!global_access_node) {
        LOG_E(TAG_VARS, "Null Node");
        res.error = EMU_ERR_NULL_PTR;
        return res;
    }

    mem_acces_s_t *header = (mem_acces_s_t*)global_access_node;
    emu_mem_t *mem = s_mem_contexts[header->reference_id];
    
    if (!mem) {
        LOG_E(TAG_VARS, "Invalid Ref ID %d", header->reference_id);
        res.error = EMU_ERR_MEM_INVALID_REF_ID;
        return res;
    }

    uint32_t offset = 0;
    uint8_t type = 0;

    emu_err_t err = _resolve_mem_offset(global_access_node, &offset, &type);
    if (err != EMU_OK) { 
        LOG_E(TAG_VARS, "Resolve offset error %s", EMU_ERR_TO_STR(err));
        res.error = err; 
        return res; 
    }

    void *pool = _mem_get_pool_ptr(header->reference_id, type);
    if (!pool) { 
        LOG_E(TAG_VARS, "Pool null for Type %s, ctx %d", DATA_TYPE_TO_STR[type], header->reference_id);
        res.error = EMU_ERR_MEM_INVALID_DATATYPE; 
        return res; 
    }

    res.type = type;
    if (by_reference) {
        res.by_reference = 1;
        switch (type) {
            case DATA_UI8:  res.reference.u8  = &((uint8_t*)pool)[offset]; break;
            case DATA_UI16: res.reference.u16 = &((uint16_t*)pool)[offset]; break;
            case DATA_UI32: res.reference.u32 = &((uint32_t*)pool)[offset]; break;
            case DATA_I8:   res.reference.i8  = &((int8_t*)pool)[offset]; break;
            case DATA_I16:  res.reference.i16 = &((int16_t*)pool)[offset]; break;
            case DATA_I32:  res.reference.i32 = &((int32_t*)pool)[offset]; break;
            case DATA_F:    res.reference.f   = &((float*)pool)[offset]; break;
            case DATA_D:    res.reference.d   = &((double*)pool)[offset]; break;
            case DATA_B:    res.reference.b   = &((bool*)pool)[offset]; break;
            default:        res.error = EMU_ERR_MEM_INVALID_DATATYPE;
        }
    } else {
        res.by_reference = 0;
        switch (type) {
            case DATA_UI8:  res.data.u8  = ((uint8_t*)pool)[offset]; break;
            case DATA_UI16: res.data.u16 = ((uint16_t*)pool)[offset]; break;
            case DATA_UI32: res.data.u32 = ((uint32_t*)pool)[offset]; break;
            case DATA_I8:   res.data.i8  = ((int8_t*)pool)[offset]; break;
            case DATA_I16:  res.data.i16 = ((int16_t*)pool)[offset]; break;
            case DATA_I32:  res.data.i32 = ((int32_t*)pool)[offset]; break;
            case DATA_F:    res.data.f   = ((float*)pool)[offset]; break;
            case DATA_D:    res.data.d   = ((double*)pool)[offset]; break;
            case DATA_B:    res.data.b   = ((bool*)pool)[offset]; break;
            default:        res.error = EMU_ERR_MEM_INVALID_DATATYPE;
        }
    }
    return res;
}

emu_err_t mem_set(void *global_access_node, emu_variable_t val) {
    mem_acces_s_t *header = (mem_acces_s_t*)global_access_node;
    
    if (!_get_mem_context(header->reference_id)) {
        LOG_E(TAG_VARS, "Invalid Context %d", header->reference_id);
        return EMU_ERR_MEM_INVALID_REF_ID;
    }

    emu_variable_t dest = mem_get(global_access_node, true);
    
    if (dest.error != EMU_OK) return dest.error;
    if (!dest.by_reference) {
        LOG_E(TAG_VARS, "Can't set value, by reference not provided, type %s id %d", DATA_TYPE_TO_STR[header->target_type], header->target_idx);
        return EMU_ERR_MEM_INVALID_DATATYPE;}

    double src_val = emu_var_to_double(val);
    double rnd = (dest.type < DATA_F) ? round(src_val) : src_val;

    switch (dest.type) {
        case DATA_UI8:  *dest.reference.u8  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); break;
        case DATA_UI16: *dest.reference.u16 = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); break;
        case DATA_UI32: *dest.reference.u32 = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); break;
        case DATA_I8:   *dest.reference.i8  = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); break;
        case DATA_I16:  *dest.reference.i16 = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); break;
        case DATA_I32:  *dest.reference.i32 = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); break;
        
        case DATA_F:    *dest.reference.f   = (float)src_val; break;
        case DATA_D:    *dest.reference.d   = src_val; break;
        
        case DATA_B:    *dest.reference.b   = (src_val != 0.0); break;
        
        default:        return EMU_ERR_MEM_INVALID_DATATYPE;
    }
    return EMU_OK; 
}

double emu_var_to_double(emu_variable_t v) {
    if (v.by_reference) {
        switch (v.type) {
            case DATA_UI8:  return (double)(*v.reference.u8);
            case DATA_UI16: return (double)(*v.reference.u16);
            case DATA_UI32: return (double)(*v.reference.u32);
            case DATA_I8:   return (double)(*v.reference.i8);
            case DATA_I16:  return (double)(*v.reference.i16);
            case DATA_I32:  return (double)(*v.reference.i32);
            case DATA_F:    return (double)(*v.reference.f);
            case DATA_D:    return (*v.reference.d);
            case DATA_B:    return (*v.reference.b) ? 1.0 : 0.0;
            default: return 0.0;
        }
    } else {
        switch (v.type) {
            case DATA_UI8:  return (double)v.data.u8;
            case DATA_UI16: return (double)v.data.u16;
            case DATA_UI32: return (double)v.data.u32;
            case DATA_I8:   return (double)v.data.i8;
            case DATA_I16:  return (double)v.data.i16;
            case DATA_I32:  return (double)v.data.i32;
            case DATA_F:    return (double)v.data.f;
            case DATA_D:    return v.data.d;
            case DATA_B:    return v.data.b ? 1.0 : 0.0;
            default: return 0.0;
        }
    }
}

/*********************************************************************************
 * 
 * REFERENCES
 * 
 *******************************************************************************/
static mem_pool_acces_s_t s_scalar_pool;
static mem_pool_acces_arr_t  s_arena_pool;
static bool s_is_initialized = false;

void emu_refs_system_init(size_t max_scalars, size_t arena_bytes) {
    if (s_is_initialized) emu_refs_system_free();
    
    if (mem_pool_access_scalar_create(&s_scalar_pool, sizeof(mem_acces_s_t), max_scalars, "REF_SCAL") != EMU_OK) {
        LOG_E(TAG_REF, "Scalar Pool Fail");
        return;
    }
    if (!mem_pool_acces_arr_create(&s_arena_pool, arena_bytes, "REF_ARR")) {
        LOG_E(TAG_REF, "Arena Pool Fail");
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        return;
    }
    s_is_initialized = true;
    LOG_I(TAG_REF, "Scalars=%d, Arena=%d bytes", (int)max_scalars, (int)arena_bytes);
}

void emu_refs_system_free(void) {
    if (s_is_initialized) {
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        mem_pool_acces_arr_destroy(&s_arena_pool);
        s_is_initialized = false;
        LOG_I(TAG_REF, "Refs System Freed");
    }
}

static void* _parse_node_recursive(uint8_t **cursor, size_t *len) {
    if (*len < 3) {
        LOG_E(TAG_REF, "Incomplete header (len=%d)", (int)*len);
        return NULL;
    }
    uint8_t h = (*cursor)[0];
    uint8_t dims = h & 0x0F;
    uint8_t type = (h >> 4) & 0x0F;
    
    uint16_t idx; memcpy(&idx, &(*cursor)[1], 2);
    *cursor += 3; *len -= 3;

    if (dims == 0) {
        if (*len < 1) {
            LOG_E(TAG_REF, "Incomplete config byte");
            return NULL;
        }
        uint8_t ref_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        mem_acces_s_t *n = (mem_acces_s_t*)mem_pool_acces_scalar_alloc(&s_scalar_pool);
        if (!n) {
            LOG_E(TAG_REF, "Scalar Pool Exhausted!");
            return NULL;
        }
        
        n->dims_cnt = 0; 
        n->target_type = type; 
        n->target_idx = idx;
        n->reference_id = ref_byte & 0x0F;
        
        return n;
    } else {
        if (*len < 1) {
            LOG_E(TAG_REF, "Incomplete config byte");
            return NULL;
        }
        uint8_t config_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        size_t size = sizeof(mem_acces_arr_t) + (dims * sizeof(idx_u));
        mem_acces_arr_t *n = (mem_acces_arr_t*)mem_pool_acces_arr_alloc(&s_arena_pool, size);
        if (!n) {
            LOG_E(TAG_REF, "Ref Arena Exhausted! Need %d bytes", (int)size);
            return NULL;
        }
        
        n->dims_cnt = dims; 
        n->target_type = type; 
        n->target_idx = idx; 
        n->reference_id = config_byte & 0x0F;
        n->idx_types = (config_byte >> 4) & 0x0F;

        for (int i = 0; i < dims; i++) {
            if (IDX_IS_DYNAMIC(n, i)) {
                n->indices[i].next_node = _parse_node_recursive(cursor, len);
                if (!n->indices[i].next_node) return NULL; // Error propagated from child
            } else { 
                if (*len < 2) {
                    LOG_E(TAG_REF, "Incomplete static index (Dim %d)", i);
                    return NULL;
                }
                memcpy(&n->indices[i].static_idx, *cursor, 2); 
                *cursor += 2; *len -= 2; 
            }
        }
        return n;
    }
}

emu_result_t emu_parse_block_inputs(chr_msg_buffer_t *source, block_handle_t *block) {
    
    uint8_t *data; size_t len, b_size = chr_msg_buffer_size(source), found = 0;
    
    for (size_t i = 0; i < b_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len > 5 && data[0] == 0xBB && data[2] >= 0xF0) {
            uint16_t b_idx; memcpy(&b_idx, &data[3], 2);
            if (b_idx == block->cfg.block_idx){
                uint8_t r_idx = data[2] - 0xF0;
                if (r_idx < block->cfg.in_cnt) {
                    uint8_t *ptr = &data[5]; size_t pl_len = len - 5;
                    LOG_I(TAG, "Parsing Input %d for block %d", r_idx, b_idx);
                    block->inputs[r_idx] = _parse_node_recursive(&ptr, &pl_len);
                    if (!block->inputs[r_idx]) {
                        LOG_E(TAG_REF, "Input Parse Failed Block %d Input %d", b_idx, r_idx);
                        return EMU_RESULT_CRITICAL(EMU_ERR_INVALID_DATA, i);
                    }
                    found++;
                }
            }
        }
    }
    return EMU_RESULT_OK();
}

emu_result_t emu_parse_block_outputs(chr_msg_buffer_t *source, block_handle_t *block) {
    uint8_t *data; 
    size_t len;
    size_t b_size = chr_msg_buffer_size(source);
    uint8_t found = 0;
    
    for (size_t i = 0; i < b_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len > 5 && data[0] == 0xBB && data[2] >= 0xE0 && data[2] <= 0xEF) {
            
            uint16_t b_idx; 
            memcpy(&b_idx, &data[3], 2);
            
            if (b_idx == block->cfg.block_idx) {
                uint8_t r_idx = data[2] - 0xE0;
                
                if (r_idx < block->cfg.q_cnt) {
                    uint8_t *ptr = &data[5]; 
                    
                    uint8_t h = ptr[0];
                    uint8_t type = (h >> 4) & 0x0F;
                    
                    uint16_t target_idx; 
                    memcpy(&target_idx, &ptr[1], 2);
                    
                    uint8_t config_byte = ptr[3];
                    uint8_t ref_id = config_byte & 0x0F;
                    LOG_I(TAG, "Parsing Output %d for block %d", r_idx, b_idx);
                    emu_mem_t *target_mem = _get_mem_context(ref_id);
                    if (!target_mem) {
                        LOG_E(TAG_REF, "Invalid Ctx ID %d", ref_id);
                        return EMU_RESULT_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, i);
                    }

                    emu_mem_instance_iter_t meta = _mem_get_instance(target_mem, type, target_idx);
                    
                    if (meta.raw == NULL) {
                        LOG_E(TAG_REF, "Meta NULL Ctx:%d T:%d I:%d", ref_id, type, target_idx);
                        return EMU_RESULT_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, i);
                    }

                    block->outputs[r_idx].raw = meta.raw;
                    
                    found++;
                    LOG_I(TAG, "Block %d Output %d linked to MemInst %p", b_idx, r_idx, meta.raw);
                }
            }
        }
    }

    return EMU_RESULT_OK();
}