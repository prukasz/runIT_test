#include "emu_logging.h"
#include "mem_types.h"
#include "emu_helpers.h"
#include "emu_variables.h"
#include "emu_variables_acces.h"
#include "string.h"
static const char* TAG = __FILE_NAME__;

/******************************************************************************************************************************
 * Slab alocator is common for all mem_access_t struct among code,
 * total_indices describe extra indices used to fetch value from table
 * 
 * 
 *****************************************************************************************************************************/

static struct{
    uint8_t* data_slab;
    uint32_t capacity;
    uint32_t next_addr;
    bool created;
}mem_access_data;

#define PKT_ACCES_DATA_SIZE 2*sizeof(uint16_t)

void mem_access_free_space(void){
    if (mem_access_data.data_slab) {free(mem_access_data.data_slab);}
    mem_access_data.data_slab = NULL;
    mem_access_data.created = 0;
    mem_access_data.capacity =0;
    mem_access_data.next_addr=0;
}

#undef OWNER
#define OWNER EMU_OWNER_mem_access_allocate_space
emu_result_t mem_access_allocate_space(uint16_t references_count, uint16_t total_indices){
    uint32_t total_bytes = (references_count* __builtin_align_up(sizeof(mem_access_t),4)) + (total_indices*sizeof(uint32_t));
    if (mem_access_data.created) {mem_access_free_space();}
    mem_access_data.data_slab = (uint8_t*)calloc(total_bytes, sizeof(uint8_t));
    if (!mem_access_data.data_slab){RET_E(EMU_ERR_NO_MEM, "Not enough space for all references");}
    mem_access_data.created = 1;
    mem_access_data.capacity = references_count;
    return EMU_RESULT_OK();
}

mem_access_t* mem_access_new(uint8_t extra_indices){
    if (!mem_access_data.created) return NULL;
    //we calculate for ptr but can use space for normal "values"
    uint8_t size_to_take = sizeof(mem_access_t)+extra_indices*sizeof(mem_access_t*);
    uint8_t size_aligned = __builtin_align_up(size_to_take, 4);
    if(size_aligned + mem_access_data.next_addr > mem_access_data.capacity){return NULL;}
    mem_access_t* tmp = (mem_access_t*)&mem_access_data.data_slab[mem_access_data.next_addr];
    mem_access_data.next_addr += size_aligned;
    return tmp;
}


#undef OWNER
#define OWNER EMU_OWNER_emu_mem_parse_access_create
emu_result_t emu_mem_parse_access_create(const uint8_t*data, const uint16_t el_cnt, void* nothing){
    if(el_cnt!=PKT_ACCES_DATA_SIZE){RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet for mem access storage space incomplete");}
    uint16_t ref_cnt = parse_get_u16(data, 0);
    uint16_t total_indices = parse_get_u16(data, 2);
    return mem_access_allocate_space(ref_cnt, total_indices);
}

typedef struct __packed{
    uint8_t  type       :4;
    uint8_t  ctx_id     :3;
    uint8_t  is_index_resolved:1;
    uint8_t  dims_cnt   :3;
    uint8_t  idx_type   :3;
    uint8_t  reserved   :2; 
    uint16_t instance_idx;
}access_packet;

/**
 * @brief Parse access struct;
 * @note packet looks like access_packet + if dims cnt > 0, idx1[access_packet or uin16_t (check idx_type), recursive....][idx2...]
 */
emu_err_t emu_mem_parse_access(const uint8_t *data, const uint16_t el_cnt, uint16_t* idx, mem_access_t **out_ptr) {
    if (*idx + sizeof(access_packet) > el_cnt) return EMU_ERR_INVALID_PACKET_SIZE;
    access_packet head;
    memcpy(&head, data + *idx, sizeof(access_packet));
    *idx += sizeof(access_packet);

    // 3. Allocate this Node
    mem_access_t* me = mem_access_new(head.dims_cnt);
    if (!me) return EMU_ERR_NO_MEM;

    // Link Target (Look up the actual instance pointer from global context)
    // Note: Add safety checks here (ctx_id < MAX, type < TYPES)
    me->instance = &mem_contexts[head.ctx_id].types[head.type].instances[head.instance_idx];
    me->indices_cnt = head.dims_cnt;
    me->is_idx_static_mask = head.idx_type; // Store mask: bit=1 means static, bit=0 means dynamic

    // 4. Base Case: No Dimensions (Scalar Access)
    if (head.dims_cnt == 0) {
        me->is_index_resolved = 1;
        *out_ptr = me; // Return the object
        return EMU_OK;
    }

    // 5. Recursive Case: Array Access
    bool all_static = true;

    for (uint8_t i = 0; i < head.dims_cnt; i++) {
        // Check Bitmask: Is this index Static (1) or Dynamic (0)?
        // Note: idx_type is only 3 bits, so this only works for 3 dims!
        bool is_static = (head.idx_type >> i) & 0x01;

        if (is_static) {
            if (*idx + 2 > el_cnt) return EMU_ERR_INVALID_PACKET_SIZE;
            me->indices_values[i].static_index = parse_get_u16(data, *idx); // Use your helper
            *idx += 2;
        } 
        else {
            all_static = false;
            emu_err_t err = emu_mem_parse_access(data, el_cnt, idx, &me->indices_values[i].dynamic_index);
            if (err != EMU_OK) return err;
        }
    }

    if (all_static) {
        uint32_t final_offset = 0;
        uint32_t stride = 1;
         
        for (int8_t i = head.dims_cnt - 1; i >= 0; i--) {
            uint16_t index_val = me->indices_values[i].static_index;
            
            uint16_t dim_size = mem_contexts[head.ctx_id].types[head.type].dims_pool[me->instance->dims_idx + i];

            if (index_val >= dim_size) {return EMU_ERR_MEM_OUT_OF_BOUNDS;}

            final_offset += index_val * stride;
            stride *= dim_size; // Accumulate stride for next dimension
        }

        me->resolved_index = final_offset;
        me->is_index_resolved = 1;
    } else {
        me->is_index_resolved = 0;
        me->resolved_index = 0;
    }
    *out_ptr = me;
    return EMU_OK;
}

#undef OWNER
#define OWNER EMU_OWNER_mem_get
emu_result_t mem_get(mem_var_t *result, const mem_access_t *search, bool by_reference){
    mem_instance_t *instance = search->instance;
    uint8_t type = instance->type;
    uint16_t el_offset = 0;
    
    // Fast path: resolved index (scalars or pre-computed arrays)
    if (likely(search->is_index_resolved)) {
        el_offset = search->resolved_index;
    }

    // Slow path: dynamic index resolution
    else if (unlikely(search->indices_cnt > 0)) {

        uint16_t *dims_pool = mem_contexts[instance->context].types[type].dims_pool;
        uint16_t dims_base = instance->dims_idx;
        uint16_t stride = 1; 
        
        for (int8_t i = search->indices_cnt - 1; i >= 0; i--) {
            uint16_t index_val;
            uint16_t dim_size = dims_pool[dims_base + i];  
            
            if((search->is_idx_static_mask >> i) & 0x01){
                index_val = search->indices_values[i].static_index;
            }else{
                mem_var_t v;
                emu_result_t res = mem_get(&v, search->indices_values[i].dynamic_index, false);
                if(unlikely(res.code != EMU_OK)){RET_ED(res.code, 0, ++res.depth, "Recursive mem_get failed %s", EMU_ERR_TO_STR(res.code));}
                index_val = MEM_CAST(v, (uint16_t)0);
            }
            
            if (unlikely(index_val >= dim_size)) {RET_E(EMU_ERR_MEM_OUT_OF_BOUNDS, "Index OOB %d>=%d", index_val, dim_size);}
            el_offset += index_val * stride;
            stride *= dim_size;
        }
    }
    
    // Build result
    result->type = type;
    
    if(by_reference){
        result->by_reference = true;
        result->data.ptr.u8 = instance->data.u8 + el_offset * MEM_TYPE_SIZES[type];
    }else{
        result->by_reference = false;
        // Direct pointer arithmetic instead of switch for better performance
        switch (type) {
            case MEM_B:   result->data.val.b   = instance->data.b[el_offset];   break;
            case MEM_F:   result->data.val.f   = instance->data.f[el_offset];   break;
            case MEM_U8:  result->data.val.u8  = instance->data.u8[el_offset];  break;
            case MEM_U16: result->data.val.u16 = instance->data.u16[el_offset]; break;
            case MEM_U32: result->data.val.u32 = instance->data.u32[el_offset]; break;
            case MEM_I16: result->data.val.i16 = instance->data.i16[el_offset]; break;
            case MEM_I32: result->data.val.i32 = instance->data.i32[el_offset]; break;
        }
    }
    
    return EMU_RESULT_OK();
}


/**
 * @brief Sets a value in memory based on a mem_access_t descriptor
 * @param to_set The value to write (variable type)
 * @param target The access descriptor (resolved or unresolved)
 */
#undef OWNER
#define OWNER EMU_OWNER_mem_set
emu_result_t mem_set(const mem_var_t to_set, const mem_access_t *target) {
    mem_var_t dst;
    
    // Get target pointer (by_reference=true for direct write)
    emu_result_t res = mem_get(&dst, target, true);
    if (unlikely(res.code != EMU_OK)) {RET_ED(res.code, 0, 0, "Failed to resolve target: %s", EMU_ERR_TO_STR(res.code));}
    
    // Always mark as updated
    target->instance->updated = 1;
    
    // Fast path: matching types - direct copy without conversion
    if (likely(to_set.type == dst.type)) {
        switch (dst.type) {
            case MEM_B:   *dst.data.ptr.b   = to_set.data.val.b;   return EMU_RESULT_OK();
            case MEM_F:   *dst.data.ptr.f   = to_set.data.val.f;   return EMU_RESULT_OK();
            case MEM_U8:  *dst.data.ptr.u8  = to_set.data.val.u8;  return EMU_RESULT_OK();
            case MEM_U16: *dst.data.ptr.u16 = to_set.data.val.u16; return EMU_RESULT_OK();
            case MEM_U32: *dst.data.ptr.u32 = to_set.data.val.u32; return EMU_RESULT_OK();
            case MEM_I16: *dst.data.ptr.i16 = to_set.data.val.i16; return EMU_RESULT_OK();
            case MEM_I32: *dst.data.ptr.i32 = to_set.data.val.i32; return EMU_RESULT_OK();
        }
    }
    
    // Slow path: type conversion required
    float src_val = MEM_CAST(to_set, (float)0);
    
    switch (dst.type) {
        case MEM_U8:  
            *dst.data.ptr.u8  = CLAMP_CAST(roundf(src_val), 0, UINT8_MAX, uint8_t); 
            break;
        case MEM_U16: 
            *dst.data.ptr.u16 = CLAMP_CAST(roundf(src_val), 0, UINT16_MAX, uint16_t); 
            break;
        case MEM_U32: 
            *dst.data.ptr.u32 = CLAMP_CAST(roundf(src_val), 0, UINT32_MAX, uint32_t); 
            break;
        case MEM_I16: 
            *dst.data.ptr.i16 = CLAMP_CAST(roundf(src_val), INT16_MIN, INT16_MAX, int16_t); 
            break;
        case MEM_I32: 
            *dst.data.ptr.i32 = CLAMP_CAST(roundf(src_val), INT32_MIN, INT32_MAX, int32_t); 
            break;
        case MEM_F:   
            *dst.data.ptr.f   = src_val; 
            break;
        case MEM_B:   
            *dst.data.ptr.b   = (src_val != 0.0f); 
            break;
        default: 
            RET_E(EMU_ERR_MEM_INVALID_DATATYPE, "Invalid Destination Type %d", dst.type);
    }
    
    return EMU_RESULT_OK();
}



