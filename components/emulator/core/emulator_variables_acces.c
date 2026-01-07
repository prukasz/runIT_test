#include "emulator_variables_acces.h"
#include "emulator_blocks.h"
#include "emulator_types.h"
#include "emulator_errors.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h> 

static const char *TAG_VARS = "EMU_VARS";
static const char *TAG_REF  = "EMU_REF";
static const char *TAG      = "EMU_MEM";

// ============================================================================
// POOL MANAGEMENT (For References)
// ============================================================================

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

static void* mem_pool_access_arr_alloc(mem_pool_acces_arr_t *pool, size_t size) {
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

// ============================================================================
// INTERNAL RESOLVERS
// ============================================================================

extern emu_mem_t* s_mem_contexts[];

__attribute__((always_inline)) inline emu_mem_instance_iter_t _mem_get_instance(emu_mem_t *mem, uint8_t type, uint16_t idx) {
    emu_mem_instance_iter_t iter = {0};
    if (mem && idx < mem->instances_cnt[type]) {
        iter.raw = mem->instances[type][idx];
    }
    return iter;
}

__always_inline static inline emu_err_t _resolve_mem_offset(void *access_node, uint32_t *out_offset, uint8_t *out_type) {

    mem_acces_instance_iter_t target;
    target.raw = (uint8_t*)access_node;

    uint8_t  dims_cnt    = target.single->dims_cnt;
    uint8_t  target_type = target.single->target_type;
    uint16_t instance_idx  = target.single->instance_idx;

    *out_type = target_type;
    emu_mem_t *target_mem = s_mem_contexts[target.single->context_id]; 
    
    LOG_D(TAG, "Resolving access to ctx %d, type %d, idx %d", target.single->context_id, target_type, instance_idx);

    emu_mem_instance_iter_t meta;
    meta.raw = (uint8_t*)target_mem->instances[target_type][instance_idx];
    

    if (unlikely(!meta.raw)) {
        LOG_E(TAG, "Null instance for Type:%s Idx:%d", DATA_TYPE_TO_STR[target_type], instance_idx);
        return EMU_ERR_MEM_OUT_OF_BOUNDS;
    }

    //this is in case that variable wasn't resolved
    if (unlikely(dims_cnt == 0)) {
        *out_offset = meta.single->start_idx;
        return EMU_OK;
    }

    mem_access_arr_t        *acc_arr      = target.arr; 
    emu_mem_instance_arr_t *instance_arr = meta.arr;

    uint32_t final_offset = instance_arr->start_idx;
    uint32_t stride = 1;

    for (int8_t i = (int8_t)dims_cnt - 1; i >= 0; i--) {
        uint32_t index_val = 0;

        if (IDX_IS_DYNAMIC(acc_arr, i)) { 
            //if index is dynamic then get value for index
            emu_variable_t idx_var = mem_get(acc_arr->indices[i].next_instance, false);
            if (unlikely(idx_var.error)) {return idx_var.error;}

            index_val = MEM_CAST(idx_var, (uint32_t)0);
            //else just use provided static 
        }else{index_val = acc_arr->indices[i].static_idx;}

        if (unlikely(index_val >= instance_arr->dim_sizes[i])) {
            LOG_E(TAG, "Array Index OOB Dim %d: %ld >= %d", i, index_val, instance_arr->dim_sizes[i]);
            return EMU_ERR_MEM_OUT_OF_BOUNDS; 
        }

        final_offset += index_val * stride;
        stride *= instance_arr->dim_sizes[i];
    }

    *out_offset = final_offset;
    return EMU_OK;
}


//This function must be as fast as possible as we use it very ofter, for nearly every operation
emu_variable_t mem_get(void *mem_access_x, bool by_reference) {
    emu_variable_t var = {0};
    
    if (unlikely(!mem_access_x)) {var.error = EMU_ERR_NULL_PTR;goto error;}

    mem_access_s_t *target = (mem_access_s_t*)mem_access_x;
    
    uint8_t type = target->target_type;
    var.type = type;

    emu_mem_t *mem = s_mem_contexts[target->context_id];

    //if variable has static adress 
    if (likely(target->is_resolved)) {
        
        if (by_reference) {
            var.by_reference = 1;
            switch (type) {
                case DATA_UI8:  var.reference.u8  = target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.reference.u16 = target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.reference.u32 = target->direct_ptr.static_ui32; break;
                case DATA_I8:   var.reference.i8  = target->direct_ptr.static_i8; break;
                case DATA_I16:  var.reference.i16 = target->direct_ptr.static_i16; break;
                case DATA_I32:  var.reference.i32 = target->direct_ptr.static_i32; break;
                case DATA_F:    var.reference.f   = target->direct_ptr.static_f; break;
                case DATA_D:    var.reference.d   = target->direct_ptr.static_d; break;
                case DATA_B:    var.reference.b   = target->direct_ptr.static_b; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    LOG_E(TAG_VARS, "Invalid Type %d (res ref)", type);
                    return var;
            }
        } else {
            var.by_reference = 0;
            switch (type) {
                case DATA_UI8:  var.data.u8  = *target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.data.u16 = *target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.data.u32 = *target->direct_ptr.static_ui32; break;  
                case DATA_I8:   var.data.i8  = *target->direct_ptr.static_i8; break;
                case DATA_I16:  var.data.i16 = *target->direct_ptr.static_i16; break;
                case DATA_I32:  var.data.i32 = *target->direct_ptr.static_i32; break;
                case DATA_F:    var.data.f   = *target->direct_ptr.static_f; break;
                case DATA_D:    var.data.d   = *target->direct_ptr.static_d; break;
                case DATA_B:    var.data.b   = *target->direct_ptr.static_b; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    LOG_E(TAG_VARS, "Invalid Type %d (res val)", type);
                    return var;
            }
        }
    } 
    //nested indices we need to calculate offset 
    else {
        uint32_t offset = 0;
        
        var.error = _resolve_mem_offset(mem_access_x, &offset, &type);
        if (unlikely(var.error != EMU_OK)) {goto error;}

        void *pool = mem->data_pools[type];
        if (unlikely(!pool)) {var.error = EMU_ERR_NULL_PTR;goto error;}

        if (by_reference) {
            var.by_reference = 1;

            switch (type) {
                case DATA_UI8:  var.reference.u8  = &((uint8_t*)pool)[offset]; break;
                case DATA_UI16: var.reference.u16 = &((uint16_t*)pool)[offset]; break;
                case DATA_UI32: var.reference.u32 = &((uint32_t*)pool)[offset]; break;
                case DATA_I8:   var.reference.i8  = &((int8_t*)pool)[offset]; break;
                case DATA_I16:  var.reference.i16 = &((int16_t*)pool)[offset]; break;
                case DATA_I32:  var.reference.i32 = &((int32_t*)pool)[offset]; break;
                case DATA_F:    var.reference.f   = &((float*)pool)[offset]; break;
                case DATA_D:    var.reference.d   = &((double*)pool)[offset]; break;
                case DATA_B:    var.reference.b   = &((bool*)pool)[offset]; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    LOG_E(TAG_VARS, "Invalid Type %d (dyn ref)", type);
                    return var;
            }
        } else {
            var.by_reference = 0; //direct value
            switch (type) {
                case DATA_UI8:  var.data.u8  = ((uint8_t*)pool)[offset]; break;
                case DATA_UI16: var.data.u16 = ((uint16_t*)pool)[offset]; break;
                case DATA_UI32: var.data.u32 = ((uint32_t*)pool)[offset]; break;
                case DATA_I8:   var.data.i8  = ((int8_t*)pool)[offset]; break;
                case DATA_I16:  var.data.i16 = ((int16_t*)pool)[offset]; break;
                case DATA_I32:  var.data.i32 = ((int32_t*)pool)[offset]; break;
                case DATA_F:    var.data.f   = ((float*)pool)[offset]; break;
                case DATA_D:    var.data.d   = ((double*)pool)[offset]; break;
                case DATA_B:    var.data.b   = ((bool*)pool)[offset]; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    LOG_E(TAG_VARS, "Invalid Type %d (dyn val)", type);
                    return var;
            }
        }
    }
    return var; 
    error:
    //make some dump if error enabled 
    LOG_E(TAG,"Failed:%s, dupm: Type %s, Idx %d, ctx %d, resolved %d", EMU_ERR_TO_STR(var.error), DATA_TYPE_SIZES(type), target->instance_idx, target->context_id, target->is_resolved);
    return var;
}


emu_result_t mem_set(void *mem_access_x, emu_variable_t val) {
    mem_access_s_t *dst = (mem_access_s_t*)mem_access_x;
    //Here we use by reference flag, as we then can eaisly set provided value
    emu_variable_t dst_ptr = mem_get(mem_access_x, true);
    
    if (unlikely(dst_ptr.error != EMU_OK)){EMU_RETURN_CRITICAL(dst_ptr.error, 0xFFFF, TAG_VARS, "mem_get failed: %s", EMU_ERR_TO_STR(dst_ptr.error));}

    emu_mem_instance_iter_t dst_inst;
    dst_inst.raw = (uint8_t*)s_mem_contexts[dst->context_id]->instances[dst->target_type][dst->instance_idx];
    //we set flag updated as we need it for blocks
    dst_inst.single->updated = 1;

    //we use macro to do casting as we may not know type 
    double src_val = MEM_CAST(val, (double)0);

    //round for ints

    double rnd = (dst_ptr.type < DATA_F) ? round(src_val) : src_val;

    //we set value based on destination type (ceil and floor necessary as we don't need)
    switch (dst_ptr.type) {
        case DATA_UI8:  *dst_ptr.reference.u8  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); break;
        case DATA_UI16: *dst_ptr.reference.u16 = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); break;
        case DATA_UI32: *dst_ptr.reference.u32 = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); break;
        case DATA_I8:   *dst_ptr.reference.i8  = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); break;
        case DATA_I16:  *dst_ptr.reference.i16 = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); break;
        case DATA_I32:  *dst_ptr.reference.i32 = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); break;
        case DATA_F:    *dst_ptr.reference.f   = (float)src_val; break;
        case DATA_D:    *dst_ptr.reference.d   = src_val; break;
        case DATA_B:    *dst_ptr.reference.b   = (src_val != 0.0); break;
        
        default: EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, 0xFFFF, TAG_VARS, "Invalid Type %d", dst_ptr.type);
    }

    LOG_I(TAG,"set value for  %lf", src_val);
    return EMU_RESULT_OK(); 
}


/*****************************************************************************
 * 
 * References allocation (similar to instances one)
 * 
 ***************************************************************************/

 //from header
static mem_pool_acces_s_t s_scalar_pool;
static mem_pool_acces_arr_t  s_arena_pool;

static bool s_is_initialized = false;


emu_result_t emu_access_system_init(size_t max_scalars, size_t arrays_arena_bytes) {
    if (s_is_initialized) emu_access_system_free();
    emu_err_t err;
    //we create scalar pool of given count 
    if ((err=mem_pool_access_scalar_create(&s_scalar_pool, sizeof(mem_access_s_t), max_scalars, "REF_SCAL")) != EMU_OK) {
        EMU_RETURN_CRITICAL(err, 0xFFFF, TAG_REF, "Scalar Pool Fail");
    }
    //arrays pool require knowledge of extra dimensions so we provide size in bytes
    if (!mem_pool_acces_arr_create(&s_arena_pool, arrays_arena_bytes, "REF_ARR")) {
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, 0xFFFF, TAG_REF, "Arena Pool Fail");
    }
    s_is_initialized = true;
    LOG_I(TAG_REF, "Init: Scalars=%d, Arena=%d bytes", (int)max_scalars, (int)arrays_arena_bytes);
    return EMU_RESULT_OK(); // Added OK return for non-void function
}

void emu_access_system_free(void) {
    if (s_is_initialized) {
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        mem_pool_acces_arr_destroy(&s_arena_pool);
        s_is_initialized = false;
        LOG_I(TAG_REF, "Refs System Freed");
    }
}



emu_err_t mem_access_parse_node_recursive(uint8_t **cursor, size_t *len, void **out_node) {
    //verify header size
    if (*len < 3) {
        LOG_E(TAG_REF, "Incomplete header (len=%d)", (uin16_t)*len);
        return EMU_ERR_PACKET_INCOMPLETE;
    }

    //then we parse "main node header"
    uint8_t h = (*cursor)[0];
    uint8_t dims = h & 0x0F;          // 4 bity: liczba wymiarów
    uint8_t type = (h >> 4) & 0x0F;   // 4 bity: typ danych
    
    uint16_t idx; 
    memcpy(&idx, &(*cursor)[1], 2);   // 2 bajty: indeks instancji
    *cursor += 3; *len -= 3;

    //then scalar case is easy cuz we don't need anything other
    if (dims == 0) {
        if (*len < 1) {
            LOG_E(TAG_REF, "Scalar: Incomplete config byte");
            return EMU_ERR_PACKET_INCOMPLETE;
        }
        uint8_t ref_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        //we "get new node from pool"
        mem_access_s_t *n = (mem_access_s_t*)mem_pool_acces_scalar_alloc(&s_scalar_pool);
        if (!n) {
            LOG_E(TAG_REF, "Scalar Pool Exhausted!");
            return EMU_ERR_NO_MEM;
        }

        // we verify if id exists in memory, as it's required
        //we don't need to check it later 
        uint8_t ref_id = ref_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) {
            LOG_E(TAG_REF, "Invalid Ref ID %d", ref_id);
            return EMU_ERR_MEM_INVALID_REF_ID;
        }

        //We get instance 
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx];
        //check if refered instance exists in context
        if (!meta.raw) {
            LOG_E(TAG_REF, "Instance NULL Type %d Idx %d", type, idx);
            return EMU_ERR_MEM_OUT_OF_BOUNDS;
        }
        
        //We get pool to verify and assign pool address to access struct
        void *pool_base = s_mem->data_pools[type];
        if (!pool_base) return EMU_ERR_NULL_PTR;

        //fill created node 
        n->dims_cnt = 0; 
        n->target_type = type; 
        n->context_id = ref_id;
        n->instance_idx = idx; 
        n->is_resolved = 1; //scalar is always resolved 

        uint32_t final_idx = meta.single->start_idx;
        //we assign pointer to pool based on index and type (final idx is always start idx here)
        switch (type) {
            case DATA_UI8:  n->direct_ptr.static_ui8  = (uint8_t*)pool_base  + final_idx; break;
            case DATA_UI16: n->direct_ptr.static_ui16 = (uint16_t*)pool_base + final_idx; break;
            case DATA_UI32: n->direct_ptr.static_ui32 = (uint32_t*)pool_base + final_idx; break;
            case DATA_I8:   n->direct_ptr.static_i8   = (int8_t*)pool_base   + final_idx; break;
            case DATA_I16:  n->direct_ptr.static_i16  = (int16_t*)pool_base  + final_idx; break;
            case DATA_I32:  n->direct_ptr.static_i32  = (int32_t*)pool_base  + final_idx; break;
            case DATA_F:    n->direct_ptr.static_f    = (float*)pool_base    + final_idx; break;
            case DATA_D:    n->direct_ptr.static_d    = (double*)pool_base   + final_idx; break;
            case DATA_B:    n->direct_ptr.static_b    = (bool*)pool_base     + final_idx; break;
            default: return EMU_ERR_MEM_INVALID_DATATYPE;
        }

        *out_node = n;
        return EMU_OK;

    } 
    

    /******************************************************************************
    *Now if not scalar we need to check is array element static or dymanic (based on other variables)
    ********************************/
    else {
        if (*len < 1) {
            LOG_E(TAG_REF, "Array: Incomplete config byte");
            return EMU_ERR_PACKET_INCOMPLETE;
        }
        uint8_t config_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        //we need to create new access ptr, we need to enlarge it by count of dims (for idx cnt)
        size_t size = sizeof(mem_access_arr_t) + (dims * sizeof(idx_u));
        mem_access_arr_t *n = (mem_access_arr_t*)mem_pool_access_arr_alloc(&s_arena_pool, size);
        if (!n) {
            LOG_E(TAG_REF, "Ref Arena Exhausted! Need %d bytes", (int)size);
            return EMU_ERR_NO_MEM;
        }
        
        //as before we get context and check if it exists
        uint8_t ref_id = config_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) return EMU_ERR_MEM_INVALID_REF_ID;
        
        //we get instance
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx];
        if (!meta.raw) return EMU_ERR_MEM_OUT_OF_BOUNDS;

        //fill created node
        n->dims_cnt = dims; 
        n->target_type = type; 
        n->context_id = ref_id;
        n->idx_types = (config_byte >> 4) & 0x0F;
        n->instance_idx = idx;

        //we parse indices now
        bool is_dynamic = false; // this is so we know that element is static or not 
        for (int i = 0; i < dims; i++) {
            if (IDX_IS_DYNAMIC(n, i)) {
                is_dynamic = true;
                emu_err_t err = mem_access_parse_node_recursive(cursor, len, &n->indices[i].next_instance);
                if (err != EMU_OK) return err; 
            } else { 
                //this case is for static indexd (value not instance)
                if (*len < 2) {
                    LOG_E(TAG_REF, "Array: Incomplete static index (Dim %d)", i);
                    return EMU_ERR_PACKET_INCOMPLETE;
                }
                memcpy(&n->indices[i].static_idx, *cursor, 2); 
                *cursor += 2; *len -= 2; 
            }
        }

        //we get data pool for type
        void *pool_base = s_mem->data_pools[type];
        if (!pool_base) return EMU_ERR_NULL_PTR;

        //flatten array to get "real offset"
        if (!is_dynamic) {       
            uint32_t element_offset = 0;

            //Row-Major
            for (uint8_t i = 0; i < dims; i++) {
                // Bounds Check 
                if (n->indices[i].static_idx >= meta.arr->dim_sizes[i]) {
                     LOG_E(TAG_REF, "Static Index OOB: Dim %d, Idx %d >= Size %d", 
                           i, n->indices[i].static_idx, meta.arr->dim_sizes[i]);
                     return EMU_ERR_MEM_OUT_OF_BOUNDS;
                }
                if (i == 0) {
                    element_offset = n->indices[i].static_idx;
                } else {
                    element_offset = (element_offset * meta.arr->dim_sizes[i]) + n->indices[i].static_idx;
                }
            }

            //we habe final idx as start idx and offset in elements
            uint32_t final_idx = meta.arr->start_idx + element_offset;
            n->is_resolved = 1;

            switch (type) {
                case DATA_UI8:  n->direct_ptr.static_ui8  = (uint8_t*)pool_base  + final_idx; break;
                case DATA_UI16: n->direct_ptr.static_ui16 = (uint16_t*)pool_base + final_idx; break;
                case DATA_UI32: n->direct_ptr.static_ui32 = (uint32_t*)pool_base + final_idx; break;
                case DATA_I8:   n->direct_ptr.static_i8   = (int8_t*)pool_base   + final_idx; break;
                case DATA_I16:  n->direct_ptr.static_i16  = (int16_t*)pool_base  + final_idx; break;
                case DATA_I32:  n->direct_ptr.static_i32  = (int32_t*)pool_base  + final_idx; break;
                case DATA_F:    n->direct_ptr.static_f    = (float*)pool_base    + final_idx; break;
                case DATA_D:    n->direct_ptr.static_d    = (double*)pool_base   + final_idx; break;
                case DATA_B:    n->direct_ptr.static_b    = (bool*)pool_base     + final_idx; break;
                default: return EMU_ERR_MEM_INVALID_DATATYPE;
            }

        } else {            
            uint32_t base_idx = meta.arr->start_idx;
            n->is_resolved = 0;
        }
        //we set ptr 
        *out_node = n; 
        return EMU_OK;
    }
}

