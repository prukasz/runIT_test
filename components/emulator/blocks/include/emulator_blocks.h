#pragma once
#include "emulator_types.h"
#include "emulator_interface.h"
#include "emulator_variables.h"
#include "emulator_variables_acces.h"
#include "esp_log.h" 

typedef struct block_handle_s block_handle_t;
struct block_handle_s {
    __attribute((packed)) struct {
        uint16_t block_idx;
        uint8_t block_type;  
        uint16_t in_connected;
        uint8_t in_cnt;
        uint8_t q_cnt;
    } cfg;
    void *custom_data;
    void **inputs;
    emu_mem_instance_iter_t *outputs;
};

//Unified functions for parse / execution / free
typedef emu_result_t (*emu_block_func)(block_handle_t *block);
typedef emu_result_t (*emu_block_parse_func)(chr_msg_buffer_t *source, block_handle_t *block);
typedef void (*emu_block_free_func)(block_handle_t *block);
typedef emu_result_t (*emu_block_verify_func)(block_handle_t *block);


/**
 * @brief Reset all blocks in list
 */
void emu_blocks_free_all(block_handle_t **block_structs, uint16_t num_blocks); 

/**
 * @brief Get packet with total block count and create table
 */
emu_result_t emu_parse_blocks_total_cnt(chr_msg_buffer_t *source, block_handle_t ***blocks_list, uint16_t *blocks_total_cnt);

/**
 * @brief Parse blocks/block and it's content using provided functions
 */
emu_result_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t ** blocks_list, uint16_t blocks_total_cnt);

emu_result_t emu_parse_blocks_verify_all(block_handle_t **blocks_list, uint16_t blocks_total_cnt);

extern emu_mem_t* s_mem_contexts[];
 
static const char* _TAG2 = "EMU_BLOCKS";


/**
 * @brief Check if all inputs in block updated
 */
__attribute((always_inline)) static inline bool emu_block_check_inputs_updated(block_handle_t *block) {

    for (uint8_t i = 0; i < block->cfg.in_cnt; i++) {
        if ((block->cfg.in_connected >> i) & 0x01) {
            mem_acces_s_t *access_node = (mem_acces_s_t*)block->inputs[i];
            
            if (unlikely(!access_node)) {LOG_E(_TAG2, "NULL node");return false;}

            // Ensure context index is valid
            if (access_node->reference_id >= 8) return false;
            emu_mem_t *mem = s_mem_contexts[access_node->reference_id];
            
            if (unlikely(!mem)) return false; 

            emu_mem_instance_iter_t meta;
            meta.raw = (uint8_t*)mem->instances[access_node->target_type][access_node->target_idx];
            
            if (unlikely(meta.raw == NULL)) {LOG_E(_TAG2, "NULL instance");return false;}
            //if any not updated return false
            if (meta.single->updated == 0) {return false;}
        }
    }
    LOG_D(_TAG2, "All inputs updated in block %d", block->cfg.block_idx);
    return true;
}

__attribute((always_inline)) static inline emu_result_t emu_block_set_output(block_handle_t *block, emu_variable_t *var, uint8_t num) {
    if (unlikely(!block || !var)) {
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, num, _TAG2, "NULL block or var pointer");
    }
    
    if (unlikely(num >= block->cfg.q_cnt)) {
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, num, _TAG2, "Invalid Output Index");
    }

    emu_mem_instance_s_t *meta = block->outputs[num].single;
    
    if (unlikely(!meta)) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, num, _TAG2, "NULL instance pointer");
    }

    if (unlikely(meta->reference_id >= 8)) {
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, num, _TAG2, "Invalid Ref ID %d", meta->reference_id);
    }

    emu_mem_t *mem = s_mem_contexts[meta->reference_id];
    if (unlikely(!mem)) {
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, num, _TAG2, "Ctx %d NULL", meta->reference_id);
    }

    double src_val = emu_var_to_double(*var);
    double rnd = (meta->target_type < DATA_F) ? round(src_val) : src_val;
    uint32_t offset = meta->start_idx;

    void *pool_ptr = mem->data_pools[meta->target_type];
    if (unlikely(!pool_ptr)) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, num, _TAG2, "Output Pool NULL Type %d", meta->target_type);
    }

    switch (meta->target_type) {
        case DATA_UI8:  ((uint8_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); break;
        case DATA_UI16: ((uint16_t*)pool_ptr)[offset] = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); break;
        case DATA_UI32: ((uint32_t*)pool_ptr)[offset] = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); break;
        case DATA_I8:   ((int8_t*)pool_ptr)[offset]   = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); break;
        case DATA_I16:  ((int16_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); break;
        case DATA_I32:  ((int32_t*)pool_ptr)[offset]  = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); break;
        
        case DATA_F:    ((float*)pool_ptr)[offset]    = (float)src_val; break;
        case DATA_D:    ((double*)pool_ptr)[offset]   = src_val; break;
        case DATA_B:    ((bool*)pool_ptr)[offset]     = (src_val != 0.0); break;
        
        default: 
            EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, num, _TAG2, "Invalid Type %d", meta->target_type);
    }
    
    meta->updated = 1;
    return EMU_RESULT_OK();
}

static inline void emu_block_reset_outputs_status(block_handle_t *block) {
    if (unlikely(!block || block->cfg.q_cnt == 0 || !block->outputs)) {return;}
    for (uint8_t i = 0; i < block->cfg.q_cnt; i++) {
        emu_mem_instance_s_t *instance = block->outputs[i].single;
        if (likely(instance)){instance->updated = 0;}
    }
}