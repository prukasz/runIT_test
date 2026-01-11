#include "emulator_variables.h"
#include <string.h>

static const char *TAG = "EMU_VARIABLES";


/********************************************************************************************************** *
 * 
 *                           GLOBAL MEMORY CONTEXTS
 * 
 * *************************************************************************************************************/

emu_mem_t* s_mem_contexts[MAX_MEM_CONTEXTS] = {NULL};

// Internal Helper (just retrieve memory context for code clarity)
emu_mem_t* _get_mem_context(uint8_t id) {
    if (id >= MAX_MEM_CONTEXTS) return NULL;
    return s_mem_contexts[id];
}

//Register (aka add context)
static emu_result_t emu_mem_register_context(uint8_t id, emu_mem_t *mem_ptr) {
    if (id < MAX_MEM_CONTEXTS) {
        if (s_mem_contexts[id]) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_register_context, 0, 0, TAG, "Context ID %d already registered", id);}
        s_mem_contexts[id] = mem_ptr;
        EMU_RETURN_OK(EMU_LOG_context_registered, EMU_OWNER_emu_mem_register_context, 0,  TAG, "Context ID %d registered", id);
    }
    EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_register_context, 0, 0, TAG, "Context ID %d out of bounds", id);
}

//free context (selected)
static void emu_mem_free_context(uint8_t id) {
        emu_mem_t *mem = _get_mem_context(id);
        if (mem){
            for (uint8_t i = 0; i < TYPES_COUNT; i++) {
                if (mem->data_pools[i]) { free(mem->data_pools[i]); mem->data_pools[i] = NULL; }
                if (mem->instances[i])  { free(mem->instances[i]);  mem->instances[i] = NULL; }
                if (mem->instances_arenas[i])     { free(mem->instances_arenas[i]);     mem->instances_arenas[i] = NULL; }
                mem->instances_cnt[i] = 0;
            }
        }
    EMU_REPORT(EMU_LOG_context_freed, EMU_OWNER_emu_mem_free_contexts, 0, TAG, "Context ID %d freed", id);
}



void emu_mem_free_contexts() {
    for (uint8_t i = 0; i < MAX_MEM_CONTEXTS; i++) {
        emu_mem_free_context(i); //just iterate thruoug all possible, free_context will decide
    }
}



emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                                   uint16_t var_counts[TYPES_COUNT], 
                                   uint16_t inst_counts[TYPES_COUNT], 
                                   uint16_t total_dims[TYPES_COUNT]) 
{
    emu_result_t res;

    //first we check if context exist
    emu_mem_t *mem = _get_mem_context(context_id);
    
    if (!mem) { //create if already not created
        mem = calloc(1, sizeof(emu_mem_t));
        if (!mem) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Context struct alloc failed for ID %d", context_id);}
        res =emu_mem_register_context(context_id, mem);
        if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_alloc_context,0,  ++res.depth, TAG, "Context struct alloc failed for ID %d", context_id);}
    }else{
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALREADY_CREATED, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Context already created" );
    }

    //then we allocate data pools with provided size
    for (int i = 0; i < TYPES_COUNT; i++) {
        // 1. Data Pools
        if (var_counts[i] > 0) {
            mem->data_pools[i] = calloc(var_counts[i], DATA_TYPE_SIZES[i]);
            if (mem->data_pools[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Data pool alloc failed type %d count %d, context freed", i, var_counts[i]);
            }
        }
        
        //and also we create instaces space for scalars and array
        uint16_t cnt = inst_counts[i];
        mem->instances_cnt[i] = cnt;

        if (cnt > 0) {
            mem->instances[i] = calloc(cnt, sizeof(void*));   
            if (mem->instances[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Instance table alloc failed type %d count %d, context freed", i, cnt);
            }
            
            //total size is standard scalar and space for each dimension as it require extra byte to store them (value)
            size_t arena_size = (cnt * sizeof(emu_mem_instance_s_t)) + total_dims[i];

            mem->instances_arenas[i] = calloc(1, arena_size);
            if (mem->instances_arenas[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Instance arena alloc failed type %d size %d, context freed", i, (int)arena_size);
            }
            ESP_LOGI(TAG, "Ctx %d Type %d: Allocated (Vars:%d, Inst:%d, Arena:%d B)", context_id, i, var_counts[i], cnt, (int)arena_size);
        }
    }
    EMU_RETURN_OK(EMU_LOG_context_allocated, EMU_OWNER_emu_mem_alloc_context, 0,TAG, "Context %d allocated successfully.", context_id);
}



emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);  //as usual we get message from buffer
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len < 3) continue;  //check if not empty or order (len 2)

        uint16_t header = 0;
        memcpy(&header, data, 2);  //here we get specific header

        if (header == VAR_H_SIZES) {  //we check if header match 
            uint8_t ctx_id = data[2];  //context id (mem type)
            
            size_t payload_size = 3 * TYPES_COUNT * sizeof(uint16_t);   //we need to have complete packet 
            if (len != (3 + payload_size)) {  //header + id = 3
                EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_context, 0 , 0, TAG, "Packet incomplete for VAR_H_SIZES (len=%d, expected=%d)", (int)len, (int)(3 + payload_size));
            }

            uint16_t idx = 3;  //skip header then just copy
            
            uint16_t var_counts[TYPES_COUNT];
            memcpy(var_counts, &data[idx], sizeof(var_counts)); idx += sizeof(var_counts);
            
            uint16_t inst_counts[TYPES_COUNT];
            memcpy(inst_counts, &data[idx], sizeof(inst_counts)); idx += sizeof(inst_counts);
            
            uint16_t extra_dims[TYPES_COUNT];
            memcpy(extra_dims, &data[idx], sizeof(extra_dims));
            
            //create context based on fetched data by parser
            emu_result_t res = emu_mem_alloc_context(ctx_id, var_counts, inst_counts, extra_dims);
            if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_parse_create_context, 0, ++res.depth, TAG, "Context alloc failed for ID %d", ctx_id);}
            EMU_RETURN_OK(EMU_LOG_scalars_created, EMU_OWNER_emu_mem_parse_create_context, 0, TAG, "Context %d created from VAR_H_SIZES", ctx_id);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_context, 0 , 0, TAG, "Packet not found for VAR_H_SIZES");
}

emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t packet_len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    const size_t EXPECTED_PAYLOAD = TYPES_COUNT * sizeof(uint16_t); //here we just get count of scalar instances of each type

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &packet_len);
        
        if (packet_len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_SCALAR_CNT) {
            uint8_t ctx_id = data[2];
            emu_mem_t *mem = _get_mem_context(ctx_id); //in this case there is no created memory we can refer to 
            if (!mem) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0,  0, TAG, "Scalar Cnt: Ctx %d not found", ctx_id);}
            
            uint16_t idx = 3; 
            if (packet_len != (idx + EXPECTED_PAYLOAD)) {EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0,0, TAG, "Packet incomplete for VAR_H_SCALAR_CNT");}

            for (uint8_t type = 0; type < TYPES_COUNT; type++) {
                uint16_t scalar_cnt = 0;
                memcpy(&scalar_cnt, &data[idx], sizeof(uint16_t));
                idx += sizeof(uint16_t);
                
                if (scalar_cnt == 0) continue; //skip if scalar of type =  0 don't return anythig 

                if (!mem->instances_arenas[type] || !mem->instances[type]) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_INSTANCE, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0 ,0 , TAG, "Scalar Cnt: Arena/Table missing for Type %d Ctx %d", type, ctx_id);}

                uint8_t *arena_cursor = (uint8_t*)mem->instances_arenas[type];
                void **instance_table = mem->instances[type];

                for (int k = 0; k < scalar_cnt; k++) {
                    instance_table[k] = arena_cursor;

                    emu_mem_instance_s_t *meta = (emu_mem_instance_s_t*)arena_cursor;
                    
                    meta->dims_cnt     = 0;    
                    meta->target_type  = type;       
                    meta->context_id   = ctx_id; 
                    meta->updated      = 1;      
                    meta->start_idx    = k;     //start idx is for scalars equal index in instances 0-k                                   
                    LOG_I("TAG", "Ctx %d: Created SCALAR of Type %s", ctx_id, EMU_DATATYPE_TO_STR[type]);
                    arena_cursor += sizeof(emu_mem_instance_s_t);
                }
                
                mem->next_data_pools_idx[type] = scalar_cnt;  //each scalar takes up 1 index
                mem->next_instance_idx[type] = scalar_cnt; //each scalar takes up 1 index
                mem->instances_arenas_offset[type] = scalar_cnt * sizeof(emu_mem_instance_s_t);  // instances handle is always same size for scalar so we offset by total size in bytes
                    
                LOG_I(TAG, "Ctx %d: Created %d SCALARS of Type %s", ctx_id, scalar_cnt, EMU_DATATYPE_TO_STR[type]);
            } 
            EMU_RETURN_OK(EMU_LOG_scalars_created, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, TAG, "Scalars created for context %d", ctx_id);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0 , 0, TAG, "Packet not found for VAR_H_SCALAR_CNT");
}

emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source) {
    uint8_t *data;       
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_ARR) {  //again process only matching headers
            uint8_t ctx_id = data[2];
            emu_mem_t *mem = _get_mem_context(ctx_id);  //so we can refer to
            if (!mem) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Ctx %d not found", ctx_id);}

            uint16_t idx = 3; 

            while (idx < len) {
                if ((idx + 2) > len) break;  //check for end of packet

                uint8_t dims_cnt    = data[idx++];   //first dims count so we know the offset for reading
                uint8_t target_type = data[idx++];   //type directly to struct 

                //check for edge cases
                if (target_type >= TYPES_COUNT || dims_cnt > 4) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Invalid type %d or dims %d", target_type, dims_cnt);}

                //check for packtet completeness
                if ((idx + dims_cnt) > len) {EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Packet incomplete for array type %d", target_type);}

                void **instance_table = mem->instances[target_type];
                
                if (!instance_table) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_INSTANCE, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Array: Instance table missing for Type %d Ctx %d", target_type, ctx_id);}


                //we get right place to create our instance
                uint32_t instance_idx = mem->next_instance_idx[target_type];  //offsets from constants
                uint32_t data_idx     = mem->next_data_pools_idx[target_type];   //offsets from constants
                uint8_t *arena_cursor = (uint8_t*)mem->instances_arenas[target_type] + mem->instances_arenas_offset[target_type];  //offsets from constants

                if (instance_idx >= mem->instances_cnt[target_type]) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Max instances reached for type %d", target_type);}

                instance_table[instance_idx] = arena_cursor;
                //fill struct
                emu_mem_instance_arr_t *meta = (emu_mem_instance_arr_t*)arena_cursor;
                meta->dims_cnt     = dims_cnt;
                meta->target_type  = target_type;
                meta->context_id = ctx_id;
                meta->updated      = 1;
                meta->start_idx    = data_idx;

                //take a look at '[d]' we use it to copy dims dimensions (sizes of them) from packet
                uint32_t total_elements = 1;
                for (int d = 0; d < dims_cnt; d++) {
                    uint8_t dim_size = data[idx++];
                    meta->dim_sizes[d] = dim_size;
                    total_elements *= dim_size;
                }
                //we created offset to next one 

                mem->next_data_pools_idx[target_type]     += total_elements; //total elements created on the go in dims loop
                mem->next_instance_idx[target_type]++;  //instance always 1 offset
                mem->instances_arenas_offset[target_type]      += sizeof(emu_mem_instance_arr_t) + dims_cnt;  //here we mark enough space for extra dimension + array handle

                LOG_I(TAG, "Ctx %d: Created ARRAY Type %d (Dims:%d, Elems:%ld)", 
                      ctx_id, target_type, dims_cnt, total_elements);
            }
            EMU_RETURN_OK(EMU_LOG_arrays_created, EMU_OWNER_emu_mem_parse_create_array_instances, 0, TAG, "Arrays created for context %d", ctx_id);
        }
    }
     //something wrong as there should be any packet if called but nothing happes really
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_array_instances, 0 , 0, TAG, "Packet not found for VAR_H_ARR"); 
}



/**
 * @brief we match type to header enums
 */
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

// this is pure for parse so looks clearer
static inline emu_mem_instance_iter_t _mem_get_instance_meta(emu_mem_t *mem, uint8_t type, uint16_t idx) {
    emu_mem_instance_iter_t iter = {0};
    if (mem && idx < mem->instances_cnt[type]) {
        iter.raw = mem->instances[type][idx];
    }
    return iter;
}

//subfunction to parse data into scalars
static emu_result_t _parse_scalar_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id);  //again check if there is even what to fill
    if (!mem) EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Ctx %d not found", ctx_id);

    //also check if pool exist, so we can memcpy to existing place
    void *pool_ptr = mem->data_pools[type];
    if (!pool_ptr) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Data pool NULL for Ctx %d Type %d", ctx_id, type);

    size_t cursor = 0;
    size_t elem_size = DATA_TYPE_SIZES[type];  //just table of sizeof
    size_t entry_size = 2 + elem_size;  //index + value so we don't send zeros

    while (cursor + entry_size <= len) {
        uint16_t instance_idx = 0;
        memcpy(&instance_idx, &data[cursor], 2);
        cursor += 2;

        //edge case
        if (instance_idx >= mem->instances_cnt[type]) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Instance Idx OOB (%d >= %d) type %d", instance_idx, mem->instances_cnt[type], type);}

        //we get right instance metadata
        emu_mem_instance_iter_t meta = _mem_get_instance_meta(mem, type, instance_idx);
        if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Meta NULL for Inst %d Type %d", instance_idx, type);}

        //then place in pools
        uint32_t pool_idx = meta.single->start_idx;
        uint8_t *dest = (uint8_t*)pool_ptr + (pool_idx * elem_size);
        

        memcpy(dest, &data[cursor], elem_size);
        cursor += elem_size;
        meta.single->updated = 1;  //mark as updated for safety (it should be marked already when created)
    }
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER__parse_scalar_data, 0, TAG, "Scalar data parsed for Ctx %d Type %d", ctx_id, type);
}

static emu_result_t _parse_array_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id);
    //same cases as in scalars
    if (!mem) EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Ctx %d not found", ctx_id);

    if (len < 4) {EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Packet too small (len=%d)", (int)len);}

    uint16_t instance_idx = 0;
    memcpy(&instance_idx, &data[0], 2); //we get instance idx
    
    uint16_t offset_in_array = 0;
    memcpy(&offset_in_array, &data[2], 2);  //and offset in array 

    emu_mem_instance_iter_t meta = _mem_get_instance_meta(mem, type, instance_idx);
    if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Meta NULL for Inst %d", instance_idx);}

    uint32_t pool_start_idx = meta.arr->start_idx;
    uint32_t final_pool_idx = pool_start_idx + offset_in_array;

    void *pool_ptr = mem->data_pools[type];
    if (!pool_ptr) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Data pool NULL for Ctx %d Type %d", ctx_id, type);

    size_t elem_size = DATA_TYPE_SIZES[type]; //again size to copy
    size_t data_bytes = len - 4;
    
    uint8_t *dest = (uint8_t*)pool_ptr + (final_pool_idx * elem_size); //we operate in bytes not types
    memcpy(dest, &data[4], data_bytes);
    meta.arr->updated = 1;
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER__parse_array_data, 0, TAG, "Array data parsed for Ctx %d Type %d", ctx_id, type);
}

emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem_ignored) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2); 

        bool is_array = false;
        int8_t type = _get_type_from_header(header, &is_array);  //we get type from header (just map)
        emu_result_t res;

        if (type != -1) {  //-1 if unknown
            uint8_t ctx_id = data[2];  //ctx is common for whole packet
            uint8_t *payload = &data[3];
            size_t payload_len = len - 3;

            if (is_array) {
                res = _parse_array_data(ctx_id, type, payload, payload_len); //invoke right function
            } else {    
                res = _parse_scalar_data(ctx_id, type, payload, payload_len);
            }
            //check for errors form parsers
            if (res.abort) {
                EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_parse_context_data_packets, 0, ++res.depth, TAG, "Data Parse: Pkt %d Fail (Ctx:%d Type:%s Arr:%d) ", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            } else if (res.warning){
                //report warning but don't return
                EMU_REPORT(res.code, EMU_OWNER_emu_mem_parse_context_data_packets, 0, TAG, "Data Parse: Pkt %d Warn (Ctx:%d Type:%s Arr:%d) ", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            } else {
                LOG_I(TAG, "Data Parse: Pkt %d OK (Ctx:%d Type:%s Arr:%d)", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            }
        }
    }
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER_emu_mem_parse_context_data_packets, 0, TAG, "Data packets parsed successfully");
}