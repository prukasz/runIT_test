#include "block_set.h"
#include "emu_logging.h"
#include "emu_variables_acces.h" 
#include "emu_blocks.h"
#include "esp_log.h"


static const char* TAG = __FILE_NAME__;

#define BLOCK_SET_VALUE    0
#define BLOCK_SET_TARGET   1

//we use only VALUE and TARGET inputs no enable input as we check is VALUE updated

#undef OWNER
#define OWNER EMU_OWNER_block_set
emu_result_t block_set(block_handle_t block) {
    // Fast exit: check if input is updated
    if(unlikely(!block_in_updated(block, BLOCK_SET_VALUE))) { 
        RET_OKD(block->cfg.block_idx, "Block Disabled"); 
    }

    mem_access_t *src_access = block->inputs[BLOCK_SET_VALUE];
    mem_access_t *tgt_access = block->inputs[BLOCK_SET_TARGET];
    
    mem_instance_t *src_inst = src_access->instance;
    mem_instance_t *tgt_inst = tgt_access->instance;
    
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
    mem_var_t v_source;
    emu_result_t res = mem_get(&v_source, src_access, false);
    if(unlikely(res.code != EMU_OK)){
        RET_ED(res.code, block->cfg.block_idx, ++res.depth, "Failed to get source: %s", EMU_ERR_TO_STR(res.code));
    }
    
    // Get target pointer directly (by_reference=true)
    mem_var_t v_target;
    res = mem_get(&v_target, tgt_access, true);
    if(unlikely(res.code != EMU_OK)){
        RET_ED(res.code, block->cfg.block_idx, ++res.depth, "Failed to get target: %s", EMU_ERR_TO_STR(res.code));
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
    
    // Slow path: type conversion required (float intermediate)
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
            RET_ED(EMU_ERR_MEM_INVALID_DATATYPE, block->cfg.block_idx, 0, "Invalid type %d", v_target.type);
    }
    
    return EMU_RESULT_OK();
}



