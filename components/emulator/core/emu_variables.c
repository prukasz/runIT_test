#include "emu_variables.h"
#include "emu_logging.h"
#include <string.h>
#include "emu_helpers.h"

static const char *TAG = __FILE_NAME__;

__attribute__((aligned(32))) mem_context_t mem_contexts[MAX_CONTEXTS];
static bool is_ctx_allocated[MAX_CONTEXTS];


typedef struct {
    uint32_t heap_elements[MAX_CONTEXTS];  /*Capacity in elements of given type*/
    uint16_t max_instances[MAX_CONTEXTS];  /*Total isntances of given type*/
    uint16_t max_dims[MAX_CONTEXTS];       /*Sum of dimensions for every non scalar instance dims>0*/
} mem_ctx_config_t;

// CTX ID (uint8_t) + TYPES CNT (9)* mem_ctx_config_t members
#define CTX_CFG_PACKET_SIZE  sizeof(uint8_t)+MEM_TYPES_COUNT*(sizeof(uint32_t) + sizeof(uint16_t)+ sizeof(uint16_t))

#undef OWNER
#define OWNER EMU_OWNER_mem_context_delete
void mem_context_delete(uint8_t ctx_id){
    if (ctx_id >= MAX_CONTEXTS) {REP_WD(EMU_ERR_CTX_INVALID_ID, ctx_id, 0, "Invalid Context ID %d", ctx_id);}
    //get selected context
    mem_context_t* ctx = &mem_contexts[ctx_id];
    for (uint8_t i = 0; i < MEM_TYPES_COUNT; i++) {
        //for each type struct perform cleanup
        type_manager_t* mgr = &ctx->types[i];

        if (mgr->data_heap.raw != NULL) {
            free(mgr->data_heap.raw);
            mgr->data_heap.raw = NULL;
        }
        mgr->data_heap_cap   = 0;
        mgr->data_heap_cursor = 0;

        if (mgr->instances != NULL) {
            free(mgr->instances);
            mgr->instances = NULL;
        }
        mgr->instances_cap   = 0;
        mgr->instances_cursor = 0;

        if (mgr->dims_pool != NULL) {
            free(mgr->dims_pool);
            mgr->dims_pool = NULL;
        }
        mgr->dims_cursor = 0;
        mgr->dims_cap =0;
    }

    is_ctx_allocated[ctx_id] = 0;
    REP_OKD(ctx_id, "Context %d destroyed/cleaned", ctx_id);

}


#undef OWNER
#define OWNER EMU_OWNER_mem_allocate_context
emu_result_t mem_context_allocate(uint8_t ctx_id, const mem_ctx_config_t* config){
    if (ctx_id >= MAX_CONTEXTS) RET_ED(EMU_ERR_CTX_INVALID_ID, ctx_id, 0, "Context id %d exceeds MAX_CONTEXTS", ctx_id);
    if (is_ctx_allocated[ctx_id]) {
        LOG_W(TAG, "Context id %d already created, skipping", ctx_id);
        return EMU_RESULT_OK();
    }
    mem_context_t* ctx = &mem_contexts[ctx_id];
    //for each type create space of provided size
    for (uint8_t i = 0; i < MEM_TYPES_COUNT; i++) {

        if(config->heap_elements[i]){
            ctx->types[i].data_heap.raw = calloc(config->heap_elements[i], MEM_TYPE_SIZES[i]);
            ctx->types[i].data_heap_cap = config->heap_elements[i];
            if(!ctx->types[i].data_heap.raw ){goto ERROR;}
        }
        if(config->max_instances[i]){
            ctx->types[i].instances  = (mem_instance_t*)calloc(config->max_instances[i], sizeof(mem_instance_t));
            ctx->types[i].instances_cap = config->max_instances[i];
            if(!ctx->types[i].instances){goto ERROR;}
        }
        if(config->max_dims[i]){
            ctx->types[i].dims_pool = (uint16_t*)calloc(config->max_dims[i], sizeof(uint16_t));
            ctx->types[i].dims_cap = config->max_dims[i];
            if(!ctx->types[i].dims_pool){goto ERROR;}
        }
        LOG_I(TAG, "Created: ctx: %d, type: %s, instances: %d, total elements: %ld, total dims: %d", ctx_id, EMU_DATATYPE_TO_STR[i], config->max_instances[i], config->heap_elements[i], config->max_dims[i]);
    }
    //mark that context is created
    is_ctx_allocated[ctx_id] = 1;
    RET_OKD(ctx_id, "context %d created", ctx_id);

    //in case of any calloc error delete context 
    ERROR:
    mem_context_delete(ctx_id);
    RET_ED(EMU_ERR_NO_MEM, ctx_id, 0, "falied to create space context id %d", ctx_id);
}


//Instances indices are determined by packet order 
static emu_err_t context_create_instance(uint8_t ctx_id, uint8_t type, uint8_t dims_cnt, uint16_t* dims_size, bool can_clear){
    if(type>MEM_TYPES_COUNT || dims_cnt>MAX_DIMS){return EMU_ERR_INVALID_ARG;}
    if(!is_ctx_allocated[ctx_id]){return EMU_ERR_CTX_INVALID_ID;}
    mem_context_t* ctx = &mem_contexts[ctx_id];
    type_manager_t* mgr = &ctx->types[type];

    //calculate total elements required by instance
    uint32_t total_size = 1;
    for(uint8_t i = 0; i<dims_cnt; i++){total_size *= dims_size[i];}

    //check is there space (should be but who knows)
    if(mgr->data_heap_cursor+total_size > mgr->data_heap_cap) {return EMU_ERR_NO_MEM;}
    if(mgr->instances_cursor==mgr->instances_cap){return EMU_ERR_NO_MEM;}
    if (mgr->dims_cursor + dims_cnt > mgr->dims_cap) { return EMU_ERR_NO_MEM; }

    

    mem_instance_t *instance = &mgr->instances[mgr->instances_cursor];
    mgr->instances_cursor++;

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
    uint32_t byte_offset = mgr->data_heap_cursor * MEM_TYPE_SIZES[type];

    instance->data.raw = (void*)(base_addr + byte_offset);
    mgr->data_heap_cursor += total_size;
    return EMU_OK;
}



//Helper struct (this is data transmitted in packet )
typedef struct __packed {
    uint16_t context   : 3; 
    uint16_t dims_cnt  : 4;
    uint16_t type      : 4;
    uint16_t updated   : 1;
    uint16_t can_clear : 1;
    uint16_t reserved  : 3;
}instance_head_t;


#undef OWNER
#define OWNER EMU_OWNER_mem_parse_instance_packet
emu_result_t emu_mem_parse_instance_packet(const uint8_t *data,const uint16_t data_len, void* nothing) {
    instance_head_t head;

    //index in data packet
    uint16_t idx = 0;

    //dim sizes are located after instance head
    uint16_t dim_sizes[MAX_DIMS];

    while (idx < data_len) {
        
        if (idx + sizeof(instance_head_t) > data_len) {RET_E(EMU_ERR_INVALID_PACKET_SIZE, "Instances packet incomplete");}

        memcpy(&head, data + idx, sizeof(instance_head_t));
        uint16_t next_offset = idx + sizeof(instance_head_t);

        //each dim_size is uint16 
        uint16_t dims_bytes = head.dims_cnt * sizeof(uint16_t);
        
        //check if dims really provided
        if (next_offset + dims_bytes > data_len) {RET_E(EMU_ERR_INVALID_PACKET_SIZE, "Instances packet incomplete");}

        //copy provided dimensions
        if (head.dims_cnt > 0) {memcpy(dim_sizes, data + next_offset, dims_bytes);}

        //create instation with collected data
        emu_err_t err = context_create_instance(
            head.context,
            head.type, 
            head.dims_cnt, 
            dim_sizes, 
            head.can_clear
        );
        LOG_I(TAG, "Created instance in ctx %d, type %s, dims cnt %d", head.context, EMU_DATATYPE_TO_STR[head.type], head.dims_cnt);
        if(err != EMU_OK){RET_ED(err, 0, 1, "While creating instance error: %s", EMU_ERR_TO_STR(err));}
        idx = next_offset + dims_bytes;
    }

    return EMU_RESULT_OK();
}


#define CTX_CFG_PACKET_SIZE  sizeof(uint8_t)+MEM_TYPES_COUNT*(sizeof(uint32_t) + sizeof(uint16_t)+ sizeof(uint16_t))

#undef OWNER
#define OWNER EMU_OWNER_emu_mem_parse_create_context
emu_result_t emu_mem_parse_create_context(const uint8_t *data,const uint16_t data_len, void *nothing){
    if (data_len != CTX_CFG_PACKET_SIZE){RET_E(EMU_ERR_INVALID_PACKET_SIZE, "Invalid packet size for ctx config");}

    mem_ctx_config_t cfg = {0};
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];
    //get info from packet into helper struct 
    for(uint8_t i = 0; i<MEM_TYPES_COUNT; i++){
        cfg.heap_elements[i]=parse_get_u32(data, idx);
        idx+= sizeof(uint32_t);
        cfg.max_instances[i]=parse_get_u16(data, idx);
        idx+= sizeof(uint16_t);
        cfg.max_dims[i]=parse_get_u16(data, idx);
        idx+= sizeof(uint16_t);
    }
    //create context
    emu_result_t res  = mem_context_allocate(ctx_id, &cfg);
    if(res.code != EMU_OK){REP_ED(res.code, ctx_id, ++res.depth, "Failed to allocate parsed ctx %d: %s", ctx_id, EMU_ERR_TO_STR(res.code));}
    RET_OKD(ctx_id, "Succesfully created context %d", ctx_id);
}

#undef OWNER
#define OWNER EMU_OWNER_emu_mem_fill_instance_scalar
emu_result_t emu_mem_fill_instance_scalar(const uint8_t* data, const uint16_t data_len, void* nothing){
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];
    uint8_t type = data[idx++];
    uint8_t count = data[idx++];
    //we need size of element 
    size_t el_size = MEM_TYPE_SIZES[type];
    //check packet size [uint8_t ctx_id] + [uint8_t type] + [uint8_t count] + count*([uint16_t inst_idx]+[sizeof(type) data])
    if(data_len<(3*sizeof(uint8_t))+ (sizeof(uint16_t)+MEM_TYPE_SIZES[type])*count){REP_ED(EMU_ERR_INVALID_PACKET_SIZE, ctx_id, 0, "Size of instances data to fill incomplete");}

    type_manager_t *mgr = &mem_contexts[ctx_id].types[type];

    for (uint8_t i = 0; i < count; i++) {
        uint16_t inst_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        if (inst_idx >= mgr->instances_cursor) {REP_WD(EMU_ERR_MEM_INVALID_IDX, ctx_id, 0, "Invalid instance idx %d for ctx %d", inst_idx, ctx_id);}

        mem_instance_t* instance = &mgr->instances[inst_idx];

        memcpy(instance->data.raw, data + idx, el_size);
        idx += el_size;
        instance->updated = 1;
        LOG_I(TAG, "Filled scalar instance %d in ctx %d of type %s", inst_idx, ctx_id, EMU_DATATYPE_TO_STR[type]);
    }
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_mem_fill_instance_array
emu_result_t emu_mem_fill_instance_array(const uint8_t* data, const uint16_t data_len, void* nothing){
    uint16_t idx = 0;
    uint8_t ctx_id = data[idx++];
    uint8_t type = data[idx++];
    uint8_t count = data[idx++];
    size_t el_size = MEM_TYPE_SIZES[type];

    type_manager_t *mgr = &mem_contexts[ctx_id].types[type];
    for (uint8_t i = 0; i < count; i++) {
        //first get index of target instance
        uint16_t inst_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);
        //then get start index in array
        uint16_t start_idx = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        //check for size
        if (inst_idx >= mgr->instances_cursor) {RET_WD(EMU_ERR_MEM_INVALID_IDX, ctx_id, 0, "Invalid instance idx %d for ctx %d", inst_idx, ctx_id);}

        mem_instance_t* instance = &mgr->instances[inst_idx];
        //get items count 
        uint16_t items = parse_get_u16(data, idx);
        idx += sizeof(uint16_t);

        //copy required size into instance data 
        memcpy(instance->data.u8 + start_idx*el_size, data + idx, items*el_size);
        idx += items*el_size;
        instance->updated = 1; 
        LOG_I(TAG, "Filled array instance %d in ctx %d of type %s, items %d from index %d", inst_idx, ctx_id, EMU_DATATYPE_TO_STR[type], items, start_idx);
    }
    return EMU_RESULT_OK();
}

emu_err_t emu_mem_fill_instance_scalar_fast(const uint8_t* data) {
    uint16_t idx = 0;
    
    uint8_t ctx_id = data[idx++];
    uint8_t type   = data[idx++];
    uint8_t count  = data[idx++];
    
    type_manager_t *mgr = &mem_contexts[ctx_id].types[type];
    size_t el_size      = MEM_TYPE_SIZES[type];
    uint16_t max_inst   = mgr->instances_cursor;

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

    type_manager_t *mgr = &mem_contexts[ctx_id].types[type];
    size_t el_size      = MEM_TYPE_SIZES[type];
    uint16_t max_inst   = mgr->instances_cursor;

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
