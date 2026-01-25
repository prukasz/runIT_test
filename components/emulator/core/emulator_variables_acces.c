#include "emulator_logging.h"
#include "mem_types.h"
#include "emu_helpers.h"
#include "emulator_variables.h"
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

emu_result_t mem_access_allocate_space(uint16_t references_count, uint16_t total_indices){
    uint32_t total_bytes = (references_count* __builtin_align_up(sizeof(mem_access_t),4)) + (total_indices*sizeof(uint32_t));
    if (mem_access_data.created) {mem_access_free_space();}
    mem_access_data.data_slab = (uint8_t*)calloc(total_bytes, sizeof(uint8_t));
    if (!mem_access_data.data_slab){EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_access_allocate_space, 0, 0, TAG, "Not enough space for all references");}
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

/**
 * @brief create space for storeing and getting mem_acces_t structs
 * @note packet looks like uint16_t ref_cnt, uint16_t total_indices
 */
emu_result_t emu_mem_parse_access_create(const uint8_t*data, const uint16_t data_len){
    if(data_len!=PKT_ACCES_DATA_SIZE){EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_access_create, 0 ,0, TAG, "Packet for mem access storage space incomplete");}
    uint16_t ref_cnt = parse_get_u16(data, 0);
    uint16_t total_indices = parse_get_u16(data, 2);
    return mem_access_allocate_space(ref_cnt, total_indices);
}

typedef struct __attribute__((packed)){
    uint8_t  type       :4;
    uint8_t  ctx_id     :3;
    uint8_t  is_resolved:1;
    uint8_t  dims_cnt   :3;
    uint8_t  idx_type   :3;
    uint8_t  reserved   :2; 
    uint16_t instance_idx;
    uint16_t resolved_offset;
}access_packet;

/**
 * @brief Parse access struct;
 * @note packet looks like access_packet, if dims cnt > 0, idx1[access_packet or uin16_t (check idx_type), recursive....][idx2...]
 */
emu_err_t emu_mem_parse_access(const uint8_t *data, const uint16_t data_len, uint16_t* idx, mem_access_t **out_ptr) {
    if (*idx + sizeof(access_packet) > data_len) return EMU_ERR_INVALID_PACKET_SIZE;
    access_packet head;
    memcpy(&head, data + *idx, sizeof(access_packet));
    *idx += sizeof(access_packet);

    // 3. Allocate this Node
    mem_access_t* me = mem_access_new(head.dims_cnt);
    if (!me) return EMU_ERR_NO_MEM;

    // Link Target (Look up the actual instance pointer from global context)
    // Note: Add safety checks here (ctx_id < MAX, type < TYPES)
    me->instance = &mem_contexts[head.ctx_id].types[head.type].instances[head.instance_idx];
    me->idx_cnt = head.dims_cnt;

    // 4. Base Case: No Dimensions (Scalar Access)
    if (head.dims_cnt == 0) {
        me->is_resolved = 1;
        me->resolved_offset = head.resolved_offset; // Use the offset provided in packet
        *out_ptr = me; // Return the object
        return EMU_OK;
    }

    // 5. Recursive Case: Array Access
    bool all_static = true;

    for (uint8_t i = 0; i < head.dims_cnt; i++) {
        // Check Bitmask: Is this index Static (0) or Dynamic (1)?
        // Note: idx_type is only 3 bits, so this only works for 3 dims!
        bool is_dynamic = (head.idx_type >> i) & 0x01;

        if (!is_dynamic) {
            if (*idx + 2 > data_len) return EMU_ERR_INVALID_PACKET_SIZE;
            me->indices[i].static_index = parse_get_u16(data, *idx); // Use your helper
            *idx += 2;
        } 
        else {
            all_static = false;
            emu_err_t err = emu_mem_parse_access(data, data_len, idx, &me->indices[i].dynamic_index);
            if (err != EMU_OK) return err;
        }
    }

    if (all_static) {
        uint32_t final_offset = 0;
        uint32_t stride = 1;
         
        for (int8_t i = head.dims_cnt - 1; i >= 0; i--) {
            uint16_t index_val = me->indices[i].static_index;
            
            uint16_t dim_size = mem_contexts[head.ctx_id].types[head.type].dims_pool[me->instance->dims_idx + i];

            if (index_val >= dim_size) {return EMU_ERR_MEM_OUT_OF_BOUNDS;}

            final_offset += index_val * stride;
            stride *= dim_size; // Accumulate stride for next dimension
        }

        me->resolved_offset = final_offset;
        me->is_resolved = 1;
    } else {
        me->is_resolved = 0;
        me->resolved_offset = 0;
    }
    *out_ptr = me;
    return EMU_OK;
}

emu_result_t mem_get(mem_var_t *result, const mem_access_t *search, bool by_reference){
    mem_instance_t *instance = search->instance;
    mem_var_t var = {0};
    var.type = instance->type;
    uint16_t el_offset = 0;
    uint16_t el_size = DATA_TYPE_SIZES[instance->type];

    //Scalar is always resolved :}
    if (instance->dims_cnt == 0 || search->is_resolved) {
        if (search->is_resolved) {el_offset = search->resolved_offset;}
        goto SET;
    }

    //and now paths with recursive index search
    uint16_t stride = 0;
    for (int8_t i = search->idx_cnt - 1; i >= 0; i--) {
        uint16_t index_val = 0;
        uint16_t dim_size = mem_contexts[instance->context].types[instance->type].dims_pool[instance->dims_idx + i];
        if((search->is_idx_static>>i)&0x01){
            index_val = search->indices[i].static_index;
        }else{
            mem_var_t v;
            //get recursive by copy
            emu_result_t res = mem_get(&v, search->indices[i].dynamic_index, 0);
            if(res.code!=EMU_OK){EMU_RETURN_CRITICAL(res.code, EMU_OWNER_mem_get, 0, ++res.depth, TAG, "Recursive mem_get failed %s", EMU_ERR_TO_STR(res.code));}
            //we cast value to max index size
            index_val = MEM_CAST(v, (uint16_t)0);
        }
        if (index_val >= dim_size) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_get, 0, 0, TAG,"Index OOB %d> %d", index_val, dim_size);}
            el_offset += index_val * stride;
            stride *= dim_size; // Accumulate stride for next dimension
    }
    

    SET:
        if(by_reference){
            var.by_reference = true;
            var.data.ptr.u8 = instance->data.u8+el_offset*el_size;
            *result = var;
        }else{
            var.by_reference = false;
            switch (instance->type)
            {
                case DATA_B: var.data.val.b = *(instance->data.b + el_offset); break;
                case DATA_F: var.data.val.f = *(instance->data.f + el_offset); break;
                case DATA_D: var.data.val.d = *(instance->data.d + el_offset); break;
                case DATA_U8: var.data.val.u8 = *(instance->data.u8 + el_offset); break;
                case DATA_U16: var.data.val.u16 = *(instance->data.u16 + el_offset); break;
                case DATA_U32: var.data.val.u32 = *(instance->data.u32 + el_offset); break;
                case DATA_I8:  var.data.val.i8 = *(instance->data.i8 + el_offset); break;
                case DATA_I16: var.data.val.i16 = *(instance->data.i16 + el_offset); break;
                case DATA_I32: var.data.val.i32 = *(instance->data.i32 + el_offset); break;
            }
            *result = var;
        }
        return EMU_RESULT_OK();
}


/**
 * @brief Sets a value in memory based on a mem_access_t descriptor
 * @param to_set The value to write (variable type)
 * @param target The access descriptor (resolved or unresolved)
 */
emu_result_t mem_set(const mem_var_t to_set, const mem_access_t *target) {
    mem_var_t dst = {0};

    //mem get with pointer
    emu_result_t res = mem_get(&dst, target, true);

    if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_mem_set, 0, 0, TAG, "Failed to resolve target: %s", EMU_ERR_TO_STR(res.code));}

    //always mark as updated
    target->instance->updated = 1;

    
    double src_val = MEM_CAST(to_set, (double)0);

    double rnd = (dst.type < DATA_F) ? round(src_val) : src_val;

    switch (dst.type) {
        case DATA_U8:  
            *dst.data.ptr.u8  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); 
            break;
        case DATA_U16: 
            *dst.data.ptr.u16 = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); 
            break;
        case DATA_U32: 
            *dst.data.ptr.u32 = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); 
            break;
            
        case DATA_I8:  
            *dst.data.ptr.i8  = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); 
            break;
        case DATA_I16: 
            *dst.data.ptr.i16 = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); 
            break;
        case DATA_I32: 
            *dst.data.ptr.i32 = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); 
            break;

        case DATA_F:   
            *dst.data.ptr.f   = (float)src_val; 
            break;
        case DATA_D:   
            *dst.data.ptr.d   = src_val; 
            break;
        case DATA_B:   
            *dst.data.ptr.b   = (src_val >= 0.0); 
            break;

        default: 
            EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_set, 0, 0, TAG, "Invalid Destination Type %d", dst.type);
    }

    // Return OK (using your result macro style)
    return EMU_RESULT_OK();
}


