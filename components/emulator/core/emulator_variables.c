#include "emulator_variables.h"
#include "emulator_logging.h"
#include <string.h>
#include "emu_helpers.h"

static const char *TAG = __FILE_NAME__;

__attribute__((aligned(32)))mem_context_t contexts[MAX_CONTEXTS];
static bool is_ctx_allocated[MAX_CONTEXTS];


typedef struct {
    uint32_t heap_elements[MAX_CONTEXTS];  // How much raw data capacity?
    uint16_t max_instances[MAX_CONTEXTS];  // How many variables?
    uint16_t max_dims[MAX_CONTEXTS];       // How many dimensions total?
} mem_ctx_config_t;

// CTX ID (uint8_t) + TYPES CNT (9)*  struct memebers
#define CTX_CFG_PACKET_SIZE  sizeof(uint8_t)+TYPES_COUNT*(sizeof(uint32_t) + sizeof(uint16_t)+ sizeof(uint16_t))

void mem_context_delete(ctx_id){
    if (ctx_id >= MAX_CONTEXTS) {EMU_RETURN_CRITICAL(EMU_ERR_CTX_INVALID_ID, EMU_OWNER_mem_context_delete, ctx_id, 0, TAG, "Invalid Context ID %d", ctx_id);}
    mem_context_t* ctx = &contexts[ctx_id];
    for (uint8_t i = 0; i < TYPES_COUNT; i++) {
        type_manager_t* mgr = &ctx->types[i];

        if (mgr->data_heap.raw != NULL) {
            free(mgr->data_heap.raw); 
            mgr->data_heap.raw = NULL;
        }
        mgr->data_heap_size   = 0;
        mgr->data_cursor = 0;

        if (mgr->instances != NULL) {
            free(mgr->instances);
            mgr->instances = NULL;
        }
        mgr->inst_cnt   = 0;
        mgr->inst_cursor = 0;

        if (mgr->dims_pool != NULL) {
            free(mgr->dims_pool);
            mgr->dims_pool = NULL;
        }
        mgr->dims_cursor = 0;
        mgr->dims_cap =0;
    }

    is_ctx_allocated[ctx_id] = 0;
    EMU_REPORT(EMU_LOG_CTX_DESTROYED, EMU_OWNER_mem_context_delete, ctx_id, TAG, "Context %d destroyed/cleaned", ctx_id);

}


emu_result_t mem_context_allocate(uint8_t ctx_id, const mem_ctx_config_t* config){
    if (ctx_id >= MAX_CONTEXTS) EMU_RETURN_CRITICAL(EMU_ERR_CTX_INVALID_ID, EMU_OWNER_mem_allocate_context, ctx_id, 0, TAG, "Context id %d exceeds MAX_CONTEXTS", ctx_id);
    if (is_ctx_allocated[ctx_id]) EMU_RETURN_CRITICAL(EMU_ERR_CTX_ALREADY_CREATED, EMU_OWNER_mem_allocate_context, ctx_id, 0, TAG, "Context id %d already created, first destroy it", ctx_id);
    mem_context_t* ctx = &contexts[ctx_id];

    for (uint8_t i = 0; i < TYPES_COUNT; i++) {

        if(config->heap_elements[i]){
            ctx->types[i].data_heap.raw = calloc(config->heap_elements[i], DATA_TYPE_SIZES[i]);
            ctx->types[i].data_heap_size = config->heap_elements[i];
            if(!ctx->types[i].data_heap.raw ){goto ERROR;}
        }
        if(config->max_instances[i]){
            ctx->types[i].instances  = (mem_instance_t*)calloc(config->max_instances[i], sizeof(mem_instance_t));
            ctx->types[i].inst_cnt = config->max_instances[i];
            if(!ctx->types[i].instances){goto ERROR;}
        }
        if(config->max_dims[i]){
            ctx->types[i].dims_pool = (uint16_t*)calloc(config->max_dims[i], sizeof(uint16_t));
            ctx->types[i].dims_cap = config->max_dims[i];
            if(!ctx->types[i].dims_pool){goto ERROR;}
        }
        LOG_I(TAG, "Created: ctx: %d, type: %s, instances: %d, total elements: %ld, total dims: %d", ctx_id, EMU_DATATYPE_TO_STR[i], config->max_instances[i], config->heap_elements[i], config->max_dims[i]);
    }
    is_ctx_allocated[ctx_id] = 1;
    EMU_RETURN_OK(EMU_LOG_ctx_created, EMU_OWNER_mem_allocate_context, ctx_id, TAG, "context %d created", ctx_id);
    ERROR:
    mem_context_delete(ctx_id);
    EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_allocate_context, ctx_id, 0,  TAG, "falied to create space context id %d", ctx_id);
}

//We asume that index is deterministic/already known;
static emu_err_t context_create_instance(uint8_t ctx_id, uint8_t type, uint8_t dims_cnt, uint16_t* dims_size, bool can_clear){
    if(type>TYPES_COUNT || dims_cnt>MAX_DIMS){return EMU_ERR_INVALID_ARG;}
    if(!is_ctx_allocated[ctx_id]){return EMU_ERR_CTX_INVALID_ID;}
    mem_context_t* ctx = &contexts[ctx_id];
    type_manager_t* mgr = &ctx->types[type];
    //total sizes
    uint32_t total_size = 1;

    for(uint8_t i = 0; i<dims_cnt; i++){total_size *= dims_size[i];}

    if(mgr->data_cursor+total_size > mgr->data_heap_size) {return EMU_ERR_NO_MEM;}
    if(mgr->inst_cursor==mgr->inst_cnt){return EMU_ERR_NO_MEM;}
    if (mgr->dims_cursor + dims_cnt > mgr->dims_cap) { return EMU_ERR_NO_MEM; }

    

    mem_instance_t *instance = &mgr->instances[mgr->inst_cursor];
    mgr->inst_cursor++;

    instance->dims_cnt = dims_cnt;
    instance->context = ctx_id;
    instance->type = type;
    if (can_clear){
        instance->updated   = 0;
        instance->can_clear = 1;
    }else{
        instance->updated   = 1;
        instance->can_clear = 0;
    }
    
    instance->dims_idx = mgr->dims_cursor;

    for(uint8_t i=0; i < dims_cnt; i++){mgr->dims_pool[mgr->dims_cursor + i] = dims_size[i];}
    mgr->dims_cursor += dims_cnt;

    uint8_t* base_addr = (uint8_t*)mgr->data_heap.raw;
    uint32_t byte_offset = mgr->data_cursor * DATA_TYPE_SIZES[type];

    instance->data.raw = (void*)(base_addr + byte_offset);
    mgr->data_cursor += total_size;
    return EMU_OK;
}

typedef struct{
    uint16_t context   : 3; 
    uint16_t dims_cnt  : 4;
    uint16_t type      : 4;
    uint16_t updated   : 1;
    uint16_t can_clear : 1;
    uint16_t reserved  : 3;
}instance_head_t;

emu_result_t mem_parse_instance_packet(uint8_t *data, uint16_t data_len) {
    instance_head_t head;
    uint16_t idx = 0;
    uint16_t dim_sizes[MAX_DIMS];

    while (idx < data_len) {
        
        if (idx + sizeof(instance_head_t) > data_len) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_PACKET_SIZE, EMU_OWNER_mem_parse_instance_packet ,0, 0 ,TAG, "Instances packet incomplete");}

        memcpy(&head, data + idx, sizeof(instance_head_t));
        
        uint16_t next_offset = idx + sizeof(instance_head_t);

        uint16_t dims_bytes = head.dims_cnt * sizeof(uint16_t);
        
        if (next_offset + dims_bytes > data_len) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_PACKET_SIZE, EMU_OWNER_mem_parse_instance_packet ,0, 0 ,TAG, "Instances packet incomplete");}

        if (head.dims_cnt > 0) {
            memcpy(dim_sizes, data + next_offset, dims_bytes);
        }


        emu_err_t err = context_create_instance(
            head.context,
            head.type, 
            head.dims_cnt, 
            dim_sizes, 
            head.can_clear
        );
        if(err != EMU_OK){EMU_RETURN_CRITICAL(err, EMU_OWNER_mem_parse_instance_packet ,0, 1 ,TAG, "While creating instance error: %s", EMU_ERR_TO_STR);}
        idx = next_offset + dims_bytes;
    }

    return EMU_RESULT_OK();
}

#define CTX_CFG_PACKET_SIZE  sizeof(uint8_t)+TYPES_COUNT*(sizeof(uint32_t) + sizeof(uint16_t)+ sizeof(uint16_t))

/**
*@brief Create context of ctx id with provided data
*@note Packet looks like uint8_t ctx_id TYPES_CNT*(uint32_t heap_elements, uint16_t max_instances, uint16_t max_dims)
*/
emu_result_t emu_mem_parse_create_context(const uint8_t *data,const uint16_t data_len){
    if (data_len != CTX_CFG_PACKET_SIZE){EMU_RETURN_CRITICAL(EMU_ERR_INVALID_PACKET_SIZE, EMU_OWNER_emu_mem_parse_create_context, 0, 0, TAG, "Invalid packet size for ctx config");}

    mem_ctx_config_t cfg = {0};
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];

    for(uint8_t i = 0; i<TYPES_COUNT; i++){
        cfg.heap_elements[i]=parse_get_u32(data, idx);
        idx+= sizeof(uint32_t);
        cfg.max_instances[i]=parse_get_u16(data, idx);
        idx+= sizeof(uint16_t);
        cfg.max_dims[i]=parse_get_u16(data, idx);
        idx+= sizeof(uint16_t);
    }
    emu_result_t res  = mem_context_allocate(ctx_id, &cfg);
    if(res.code != EMU_OK){EMU_REPORT_ERROR_CRITICAL(res.code, EMU_OWNER_emu_mem_parse_create_context, ctx_id, ++res.depth, TAG, "Failed to allocate parsed ctx %d: %s", ctx_id, EMU_ERR_TO_STR(res.code));}
    EMU_RETURN_OK(EMU_LOG_created_ctx, EMU_OWNER_emu_mem_parse_create_context, ctx_id, TAG, "Succesfully created context %d", ctx_id);
}

/**
*@brief Fill context instance with provided data (scalar)
*@note Packet looks like uint8_t ctx_id, uint8_t type, uint8_t count, count* [uint16_t instance_idx,1* sizeof(type) [data]]
*/
emu_result_t emu_mem_fill_instance_scalar(const uint8_t* data, const uint16_t data_len){
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];
    uint8_t type = data[idx++];
    uint8_t count = data[idx++];
    size_t el_size = DATA_TYPE_SIZES[type];
    if(data_len<(DATA_TYPE_SIZES[type]+sizeof(uint16_t))*count+3*sizeof(uint8_t)){EMU_REPORT_ERROR_CRITICAL(EMU_ERR_INVALID_PACKET_SIZE, EMU_OWNER_emu_mem_fill_instance_scalar, ctx_id, 0, TAG, "Size of instances data to fill incomplete")}

    type_manager_t *mgr = &contexts[ctx_id].types[type];

    for (uint8_t i = 0; i < count; i++) {
        uint16_t inst_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        if (inst_idx >= mgr->inst_cursor) {EMU_REPORT_ERROR_WARN(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER_emu_mem_fill_instance_scalar, ctx_id, 0, TAG, "Invalid instance idx %d for ctx %d", inst_idx, ctx_id);}

        mem_instance_t* instance = &mgr->instances[inst_idx];

        memcpy(instance->data.raw, data + idx, el_size);
        idx += el_size;
        instance->updated = 1;
    }
    return EMU_RESULT_OK();
}

/**
*@brief Fill context with provided data (array)
*@note Packet looks like uint8_t ctx_id, uint8_t type, uint8_t count, count* [uint16_t instance_idx, uint16_t start_idx, uint16_t items, items* sizeof(type) [data]]
*/
emu_result_t emu_mem_fill_instance_array(const uint8_t* data, const uint16_t data_len){
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];
    uint8_t type = data[idx++];
    uint8_t count = data[idx++];
    size_t el_size = DATA_TYPE_SIZES[type];

    type_manager_t *mgr = &contexts[ctx_id].types[type];
    for (uint8_t i = 0; i < count; i++) {
        uint16_t inst_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);
        uint16_t start_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        if (inst_idx >= mgr->inst_cursor) {EMU_RETURN_WARN(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER_emu_mem_fill_instance_array, ctx_id, 0, TAG, "Invalid instance idx %d for ctx %d", inst_idx, ctx_id);}

        mem_instance_t* instance = &mgr->instances[inst_idx];
        uint16_t items = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        memcpy(instance->data.u8 + start_idx*el_size, data + idx, items*el_size);
        idx += items*el_size;
        instance->updated = 1; 
    return EMU_RESULT_OK();
    }
}

emu_err_t emu_mem_fill_instance_scalar_fast(const uint8_t* data) {
    uint16_t idx = 0;
    
    uint8_t ctx_id = data[idx++];
    uint8_t type   = data[idx++];
    uint8_t count  = data[idx++];
    
    type_manager_t *mgr = &contexts[ctx_id].types[type];
    size_t el_size      = DATA_TYPE_SIZES[type];
    uint16_t max_inst   = mgr->inst_cursor;

    for (uint8_t i = 0; i < count; i++) {
        uint16_t inst_idx;
        memcpy(&inst_idx, data + idx, 2); 
        idx += 2;
        if (inst_idx >= max_inst) {return EMU_ERR_MEM_INVALID_IDX;}
        mem_instance_t* inst = &mgr->instances[inst_idx];
        memcpy(inst->data.raw, data + idx, el_size);
        
        idx += el_size;
        inst->updated = 1;
    }
    return EMU_OK;
}

emu_err_t emu_mem_fill_instance_array_fast(const uint8_t* data) {
    uint16_t idx = 0;
    
    uint8_t ctx_id = data[idx++];
    uint8_t type   = data[idx++];
    uint8_t count  = data[idx++];

    type_manager_t *mgr = &contexts[ctx_id].types[type];
    size_t el_size      = DATA_TYPE_SIZES[type];
    uint16_t max_inst   = mgr->inst_cursor;

    for (uint8_t i = 0; i < count; i++) {
        uint16_t inst_idx, start_idx, items;
        
        memcpy(&inst_idx,  data + idx, 2); idx += 2;
        memcpy(&start_idx, data + idx, 2); idx += 2;
        memcpy(&items,     data + idx, 2); idx += 2;

        uint32_t payload_bytes = items * el_size;

        if (inst_idx >= max_inst) {return EMU_ERR_MEM_INVALID_IDX;}

        mem_instance_t* inst = &mgr->instances[inst_idx];
        uint8_t* dst = inst->data.u8 + (start_idx * el_size);
        
        memcpy(dst, data + idx, payload_bytes);
        
        idx += payload_bytes;
        inst->updated = 1;
    }
    return EMU_OK;
}