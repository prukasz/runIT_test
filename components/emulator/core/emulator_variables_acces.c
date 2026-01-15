#include "emulator_variables_acces.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "mem_types.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG_VARS = "EMU_VARS";
static const char *TAG_REF  = "EMU_REF";
static const char *TAG      = "EMU_MEM";

extern emu_mem_t* s_mem_contexts[DATA_TYPES_CNT];


/**************************************************************************************************
*
* MEMORY POOL (CONTEXTS)
*
****************************************************************************************************/


/** 
* @brief stores structs for scalar access
*/
typedef struct {
    uint8_t *buffer;
    size_t   item_size;
    size_t   capacity;
    size_t   used_count;
    const char *tag;
} mem_pool_acces_s_t;


/** 
* @brief stores struct for array access
*/
typedef struct {
    uint8_t *buffer;
    size_t   total_size;
    size_t   used_bytes;
    const char *tag;
} mem_pool_acces_arr_t;


//Internal - create pool for scalars access
static emu_result_t mem_pool_access_scalar_create(mem_pool_acces_s_t *pool, size_t item_size, size_t capacity, const char *tag) {
    pool->item_size = item_size;
    pool->capacity = capacity;
    pool->used_count = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(item_size * capacity);
    //just simple verification
    if (!pool->buffer) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_mem_pool_access_scalar_create, 0, 0, TAG, "Scalar pool alloc failed for %s", tag);}
    memset(pool->buffer, 0, item_size * capacity);
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_mem_pool_access_scalar_create, 0, TAG, "Scalar pool created for %s (Item Size:%d, Capacity:%d)", tag, (int)item_size, (int)capacity);
}   
//just helper we return ptr (next one)
static void* mem_pool_acces_scalar_alloc(mem_pool_acces_s_t *pool) {
    if (pool->used_count >= pool->capacity) return NULL;
    void *ptr = pool->buffer + (pool->used_count * pool->item_size);
    pool->used_count++;
    return ptr;
}

//delete whole pool
static void mem_pool_acces_scalar_destroy(mem_pool_acces_s_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_count = 0;
}

//Create pool of given size for arrays
static emu_result_t mem_pool_acces_arr_create(mem_pool_acces_arr_t *pool, size_t size, const char *tag) {
    pool->total_size = size;
    pool->used_bytes = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(size);
    if (!pool->buffer) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_mem_pool_access_scalar_create,0, 0, TAG, "Array pool alloc failed for %s", tag);}
    memset(pool->buffer, 0, size);
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_mem_pool_access_scalar_create, 0, TAG, "Array pool created for %s (Size:%d B)", tag, (int)size);
}

//Internal for scalars (gives pointer to start of space of chosen size)
static void* mem_pool_access_arr_alloc(mem_pool_acces_arr_t *pool, size_t size) {
    size_t aligned_size = (size + 3) & ~3;
    if (pool->used_bytes + aligned_size > pool->total_size) return NULL;
    void *ptr = pool->buffer + pool->used_bytes;
    pool->used_bytes += aligned_size;
    return ptr;
}

//delete pool for arrays
static void mem_pool_acces_arr_destroy(mem_pool_acces_arr_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_bytes = 0;
}


//This function is used within mem_get to resolve array element index by recursive access
__always_inline static inline emu_err_t _resolve_mem_offset(void *access_node, uint32_t *out_offset, uint8_t *out_type) {

    mem_acces_instance_iter_t target;
    target.raw = (uint8_t*)access_node;

    uint8_t  dims_cnt    = target.single->dims_cnt;
    uint8_t  target_type = target.single->target_type;
    uint16_t instance_idx  = target.single->instance_idx;

    *out_type = target_type;
    emu_mem_t *target_mem = s_mem_contexts[target.single->context_id]; 
    
    EMU_REPORT(EMU_LOG_resolving_access, EMU_OWNER__resolve_mem_offset, 0 , TAG, "Resolving access to ctx %d, type %s, idx %d", target.single->context_id, EMU_DATATYPE_TO_STR[target_type], instance_idx);

    emu_mem_instance_iter_t meta;
    meta.raw = (uint8_t*)target_mem->instances[target_type][instance_idx];
    

    if (unlikely(!meta.raw)) {
        //just log error
        EMU_REPORT(EMU_LOG_access_out_of_bounds, EMU_OWNER__resolve_mem_offset, 0, TAG, "Null instance for Type:%s Idx:%d", EMU_DATATYPE_TO_STR[target_type], instance_idx);
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
            EMU_REPORT(EMU_LOG_access_out_of_bounds, EMU_OWNER__resolve_mem_offset, 0, TAG, "Array Index OOB Dim %d: %ld >= %d", i, index_val, instance_arr->dim_sizes[i]);
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
    uint8_t type = 0xFF;
    uint8_t ctx_id = 0xFF;

    mem_access_s_t *_target = (mem_access_s_t*)mem_access_x;
    if (unlikely(!_target)) {var.error = EMU_ERR_NULL_PTR;goto error;}

    
    type = _target->target_type; 
    var.type = type;
    ctx_id = _target->context_id;

    emu_mem_t *mem = s_mem_contexts[ctx_id];

    //if variable has static adress 
    if (likely(_target->is_resolved)) {
        
        if (by_reference) {
            var.by_reference = 1;
            switch (type) {
                case DATA_UI8:  var.reference.u8  = _target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.reference.u16 = _target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.reference.u32 = _target->direct_ptr.static_ui32; break;
                case DATA_I8:   var.reference.i8  = _target->direct_ptr.static_i8; break;
                case DATA_I16:  var.reference.i16 = _target->direct_ptr.static_i16; break;
                case DATA_I32:  var.reference.i32 = _target->direct_ptr.static_i32; break;
                case DATA_F:    var.reference.f   = _target->direct_ptr.static_f; break;
                case DATA_D:    var.reference.d   = _target->direct_ptr.static_d; break;
                case DATA_B:    var.reference.b   = _target->direct_ptr.static_b; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    EMU_REPORT(EMU_LOG_mem_invalid_data_type, EMU_OWNER_mem_get, 0, TAG_VARS, "Invalid Type %d (res ref)", type);
                    return var;
            }
        } else {
            var.by_reference = 0;
            switch (type) {
                case DATA_UI8:  var.data.u8  = *_target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.data.u16 = *_target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.data.u32 = *_target->direct_ptr.static_ui32; break;  
                case DATA_I8:   var.data.i8  = *_target->direct_ptr.static_i8; break;
                case DATA_I16:  var.data.i16 = *_target->direct_ptr.static_i16; break;
                case DATA_I32:  var.data.i32 = *_target->direct_ptr.static_i32; break;
                case DATA_F:    var.data.f   = *_target->direct_ptr.static_f; break;
                case DATA_D:    var.data.d   = *_target->direct_ptr.static_d; break;
                case DATA_B:    var.data.b   = *_target->direct_ptr.static_b; break;
                default: 
                    var.error = EMU_ERR_MEM_INVALID_DATATYPE;
                    EMU_REPORT(EMU_LOG_mem_invalid_data_type, EMU_OWNER_mem_get, 0, TAG_VARS, "Invalid Type %d (res val)", type);
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
                    EMU_REPORT(EMU_LOG_mem_invalid_data_type, EMU_OWNER__resolve_mem_offset, 0, TAG_VARS, "Invalid Type %d (dyn ref)", type);
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
    EMU_REPORT(EMU_LOG_mem_get_failed, EMU_OWNER_mem_get, 0, TAG_VARS, "Failed:%s, dump: Type %s, Idx %d, ctx %d, resolved %d", EMU_ERR_TO_STR(var.error), EMU_DATATYPE_TO_STR[type], _target->instance_idx, ctx_id, _target->is_resolved);
    return var;
}


emu_result_t mem_set(void *mem_access_x, emu_variable_t val) {
    mem_access_s_t *dst = (mem_access_s_t*)mem_access_x;
    //Here we use by reference flag, as we then can eaisly set provided value
    emu_variable_t dst_ptr = mem_get(mem_access_x, true);
    
    if (unlikely(dst_ptr.error != EMU_OK)){EMU_RETURN_CRITICAL(dst_ptr.error, EMU_OWNER_mem_set, 0, 1, TAG, "Dst error: %s", EMU_ERR_TO_STR(dst_ptr.error));}

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
        
        default: EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_set, 0, 0, TAG, "Invalid Type %d", dst_ptr.type);
    }

    EMU_RETURN_OK(EMU_LOG_mem_set, EMU_OWNER_mem_set, 0, TAG, "Set value for ctx %d, type %s, idx %d", dst->context_id, EMU_DATATYPE_TO_STR[dst->target_type], dst->instance_idx);
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
    emu_result_t res;
    //we create scalar pool of given count 
    res = mem_pool_access_scalar_create(&s_scalar_pool, sizeof(mem_access_s_t), max_scalars, "REF_SCAL");
    if(res.abort){
        mem_pool_acces_arr_destroy(&s_arena_pool);
        EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Scalar Pool Fail");}
    if(res.warning){
        s_is_initialized = true;
        EMU_RETURN_WARN(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Scalar Pool Warn");}
    
    res= mem_pool_acces_arr_create(&s_arena_pool, arrays_arena_bytes, "REF_ARR");
    if(res.abort){ 
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Arena Pool Fail");
    }
    if(res.warning){
        s_is_initialized = true;
        EMU_RETURN_WARN(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Arena Pool Warn");}
    s_is_initialized = true;
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_emu_access_system_init, 0, TAG_REF, "Init: Scalars=%d, Arena=%d bytes", (int)max_scalars, (int)arrays_arena_bytes);
}

void emu_access_system_free(void) {
    if (s_is_initialized) {
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        mem_pool_acces_arr_destroy(&s_arena_pool);
        s_is_initialized = false;
        EMU_REPORT(EMU_LOG_access_pool_allocated, EMU_OWNER_emu_access_system_free, 0, TAG_REF, "Access System Freed");
    }
}



emu_result_t mem_access_parse_node_recursive(uint8_t **cursor, size_t *len, void **out_node) {
    //verify header size
    if (*len < 3) {EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Incomplete main node header");}

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
            EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Scalar: Incomplete ref byte");
        }
        uint8_t ref_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        //we "get new node from pool"
        mem_access_s_t *n = (mem_access_s_t*)mem_pool_acces_scalar_alloc(&s_scalar_pool);
        if (!n) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Ref Arena Exhausted!");}

        // we verify if id exists in memory, as it's required
        //we don't need to check it later 
        uint8_t ref_id = ref_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Ref ID %d", ref_id);}

        //We get instance 
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx];
        //check if refered instance exists in context
        if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Out of Bounds Instance for Type %d Idx %d", type, idx);}
        
        //We get pool to verify and assign pool address to access struct
        void *pool_base = s_mem->data_pools[type];
        if (!pool_base) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Null Data Pool for Type %d", type);}

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
            default: 
                EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Data Type %d", type);
        }

        *out_node = n;
        EMU_RETURN_OK(EMU_LOG_mem_access_parse_success, EMU_OWNER_mem_access_parse_node_recursive, 0, TAG_REF, "Parsed Scalar Ref: ctx %d, type %d, idx %d", ref_id, type, idx);
    } 
    

    /******************************************************************************
    *Now if not scalar we need to check is array element static or dymanic (based on other variables)
    ********************************/
    else {
        if (*len < 1) {EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Array: Incomplete ref byte");}
        uint8_t config_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        //we need to create new access ptr, we need to enlarge it by count of dims (for idx cnt)
        size_t size = sizeof(mem_access_arr_t) + (dims * sizeof(idx_u));
        mem_access_arr_t *n = (mem_access_arr_t*)mem_pool_access_arr_alloc(&s_arena_pool, size);
        if (!n) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Ref Arena Exhausted!");}
        
        //as before we get context and check if it exists
        uint8_t ref_id = config_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Ref ID %d", ref_id);}
        
        //we get instance
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx];
        if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Out of Bounds Instance for Type %d Idx %d", type, idx);}

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
                emu_result_t res = mem_access_parse_node_recursive(cursor, len, &n->indices[i].next_instance);
                if (res.abort) {
                    EMU_RETURN_CRITICAL(res.code, EMU_OWNER_mem_access_parse_node_recursive, 0, ++res.depth, TAG_REF, "Array: Failed to parse dynamic index (Dim %d)", i);
                }
            } else { 
                //this case is for static indexd (value not instance)
                if (*len < 2) {
                    EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Array: Incomplete static index data (Dim %d)", i);
                }
                memcpy(&n->indices[i].static_idx, *cursor, 2); 
                *cursor += 2; *len -= 2; 
            }
        }

        //we get data pool for type
        void *pool_base = s_mem->data_pools[type];
        if (!pool_base) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Null Data Pool for Type %d", type);}

        //flatten array to get "real offset"
        if (!is_dynamic) {       
            uint32_t element_offset = 0;

            //Row-Major
            for (uint8_t i = 0; i < dims; i++) {
                // Bounds Check 
                if (n->indices[i].static_idx >= meta.arr->dim_sizes[i]) {
                    EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Static Index OOB: Dim %d, Idx %d >= Size %d", 
                           i, n->indices[i].static_idx, meta.arr->dim_sizes[i]);
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
                default: EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Data Type %d", type);
            }

        } else {            
            uint32_t base_idx = meta.arr->start_idx;
            n->is_resolved = 0;
        }
        //we set ptr 
        *out_node = n; 
        EMU_RETURN_OK(EMU_LOG_mem_access_parse_success, EMU_OWNER_mem_access_parse_node_recursive, 0, TAG_REF, "Parsed Array Ref: ctx %d, type %d, idx %d, dims %d", ref_id, type, idx, dims);
    }
}

