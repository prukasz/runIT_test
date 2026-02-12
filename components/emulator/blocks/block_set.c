#include "block_set.h"
#include "emu_logging.h"
#include "emu_variables_acces.h" 
#include "emu_blocks.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>

static const char* TAG = __FILE_NAME__;

#define BLOCK_SET_EN       0
#define BLOCK_SET_VALUE    1
#define BLOCK_SET_TARGET   2

/*-------------------------------BLOCK IMPLEMENTATION---------------------------------------------- */

#undef OWNER
#define OWNER EMU_OWNER_block_set
emu_result_t block_set(block_handle_t block) {
    // Check EN input first (in_0)
    if (!block_check_in_true(block, BLOCK_SET_EN)) {RET_OK_INACTIVE(block->cfg.block_idx);}

    mem_access_t *tgt_access = block->inputs[BLOCK_SET_TARGET];
    mem_instance_t *tgt_inst = tgt_access->instance;

    // Had to be created before if, cause both situtions use them
    mem_var_t v_source;
    mem_var_t v_target;

    if(!block->custom_data){
        //Fast exit: input not updated
        if(!block_in_updated(block, BLOCK_SET_VALUE)) {RET_OK_INACTIVE(block->cfg.block_idx);}

        mem_access_t *src_access = block->inputs[BLOCK_SET_VALUE];
        mem_instance_t *src_inst = src_access->instance;
        
        // ULTRA-FAST PATH: Both resolved and types match - direct pointer copy
        if(likely(src_access->is_index_resolved && tgt_access->is_index_resolved && 
                src_inst->type == tgt_inst->type)) {
            
            uint16_t src_offset = src_access->resolved_index;
            uint16_t tgt_offset = tgt_access->resolved_index;
            uint8_t type = src_inst->type;
            
            // Mark target as updated
            tgt_inst->updated = 1;
            
            // Direct memory copy based on type
            switch(type) {
                case MEM_B:   tgt_inst->data.b[tgt_offset]   = src_inst->data.b[src_offset];   return EMU_RESULT_OK();
                case MEM_F:   tgt_inst->data.f[tgt_offset]   = src_inst->data.f[src_offset];   return EMU_RESULT_OK();
                case MEM_U8:  tgt_inst->data.u8[tgt_offset]  = src_inst->data.u8[src_offset];  return EMU_RESULT_OK();
                case MEM_U16: tgt_inst->data.u16[tgt_offset] = src_inst->data.u16[src_offset]; return EMU_RESULT_OK();
                case MEM_U32: tgt_inst->data.u32[tgt_offset] = src_inst->data.u32[src_offset]; return EMU_RESULT_OK();
                case MEM_I16: tgt_inst->data.i16[tgt_offset] = src_inst->data.i16[src_offset]; return EMU_RESULT_OK();
                case MEM_I32: tgt_inst->data.i32[tgt_offset] = src_inst->data.i32[src_offset]; return EMU_RESULT_OK();
            }  
        }
        
        // STANDARD PATH: Need mem_get for dynamic resolution or type conversion
        emu_result_t res = mem_get(&v_source, src_access, false);
        if(unlikely(res.code != EMU_OK)){
            RET_ED(res.code, block->cfg.block_idx, ++res.depth, "[%"PRIu16"] Failed to get source: %s", block->cfg.block_idx, EMU_ERR_TO_STR(res.code));
        }
        
        // Get target pointer directly (by_reference=true)
        res = mem_get(&v_target, tgt_access, true);
        if(unlikely(res.code != EMU_OK)){
            RET_ED(res.code, block->cfg.block_idx, ++res.depth, "[%"PRIu16"] Failed to get target: %s", block->cfg.block_idx, EMU_ERR_TO_STR(res.code));
        }
    }
    else
    {
        v_source = *(mem_var_t*)block->custom_data;

        emu_result_t res = mem_get(&v_target, tgt_access, true);
        if(unlikely(res.code != EMU_OK)){
            RET_ED(res.code, block->cfg.block_idx, ++res.depth, "[%"PRIu16"] Failed to get target: %s", block->cfg.block_idx, EMU_ERR_TO_STR(res.code));
        }
    }
    // Mark target as updated
    tgt_inst->updated = 1;
    
    // Fast path: matching types - direct copy without conversion
    if(likely(v_source.type == v_target.type)) {
        switch(v_target.type) {
            case MEM_B:   *v_target.data.ptr.b   = v_source.data.val.b;   break;
            case MEM_F:   *v_target.data.ptr.f   = v_source.data.val.f;   break;
            case MEM_U8:  *v_target.data.ptr.u8  = v_source.data.val.u8;  break;
            case MEM_U16: *v_target.data.ptr.u16 = v_source.data.val.u16; break;
            case MEM_U32: *v_target.data.ptr.u32 = v_source.data.val.u32; break;
            case MEM_I16: *v_target.data.ptr.i16 = v_source.data.val.i16; break;
            case MEM_I32: *v_target.data.ptr.i32 = v_source.data.val.i32; break;
        }
        return EMU_RESULT_OK();
    }
    
    // Slow path: type conversion required (consider usage of double for intermediate to preserve range as for now stay with float for simplicity)
    float src_val = MEM_CAST(v_source, (float)0);
    
    switch (v_target.type) {
        case MEM_U8:  
            *v_target.data.ptr.u8  = CLAMP_CAST(roundf(src_val), 0, UINT8_MAX, uint8_t); 
            break;
        case MEM_U16: 
            *v_target.data.ptr.u16 = CLAMP_CAST(roundf(src_val), 0, UINT16_MAX, uint16_t); 
            break;
        case MEM_U32: 
            *v_target.data.ptr.u32 = CLAMP_CAST(roundf(src_val), 0, UINT32_MAX, uint32_t); 
            break;
        case MEM_I16: 
            *v_target.data.ptr.i16 = CLAMP_CAST(roundf(src_val), INT16_MIN, INT16_MAX, int16_t); 
            break;
        case MEM_I32: 
            *v_target.data.ptr.i32 = CLAMP_CAST(roundf(src_val), INT32_MIN, INT32_MAX, int32_t); 
            break;
        case MEM_F:   
            *v_target.data.ptr.f   = src_val; 
            break;
        case MEM_B:   
            *v_target.data.ptr.b   = (src_val != 0.0f); 
            break;
        default:
            RET_ED(EMU_ERR_MEM_INVALID_DATATYPE, block->cfg.block_idx, 0, "[%"PRIu16"] Invalid type %d", block->cfg.block_idx, v_target.type);
    }

    return EMU_RESULT_OK();
}

/*-------------------------------BLOCK PARSER------------------------------------------------------- */
#undef OWNER
#define OWNER EMU_OWNER_block_set_parse
emu_result_t block_set_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");

    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");

    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(mem_var_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "Alloc failed");
    }

    mem_var_t *config_value = (mem_var_t*)block->custom_data;

    if(packet_id == BLOCK_PKT_CFG){
        if(payload_len < 5) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "CONFIG payload too short");

        config_value->by_reference = false;
        uint8_t tmp = payload[0];
        config_value->type = tmp & 0x0F;

        memcpy(&config_value->data.val, &payload[1], MEM_TYPE_SIZES[config_value->type]);

        LOG_I(TAG, "Parsed CONFIG: BlockId=%"PRIu16" Type of given Value=%s", 
              block->cfg.block_idx, EMU_DATATYPE_TO_STR[config_value->type]);
    }
    
    return EMU_RESULT_OK();    
}

/*-------------------------------BLOCK VERIFIER----------------------------------------------------- */
#undef OWNER
#define OWNER EMU_OWNER_block_set_verify
emu_result_t block_set_verify(block_handle_t block) {
    if(!((block->cfg.in_connceted_mask>>BLOCK_SET_VALUE) & 1)){
        if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL %d", block->cfg.block_idx);}}
    return EMU_RESULT_OK();
}
/*-------------------------------BLOCK FREE FUNCTION------------------------------------------------ */
//handled by generic 
void block_set_free(block_handle_t block){
    if(block && block->custom_data){
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "[%d]Cleared set block data", block->cfg.block_idx);
    }
    return;
}

