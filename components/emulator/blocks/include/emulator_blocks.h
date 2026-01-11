#pragma once
#include "emulator_types.h"
#include "emulator_interface.h"
#include "emulator_variables.h"
#include "emulator_logging.h"
#include "emulator_variables_acces.h"
#include "esp_log.h" 


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

emu_result_t emu_parse_block_inputs(chr_msg_buffer_t *source, block_handle_t *block);
emu_result_t emu_parse_block_outputs(chr_msg_buffer_t *source, block_handle_t *block);

extern emu_mem_t* s_mem_contexts[];
 
static const char* _TAG2 = "EMU_BLOCKS";


/**
 * @brief Check if all inputs in block updated
 */
__attribute((always_inline)) static inline bool emu_block_check_inputs_updated(block_handle_t *block) {

    for (uint8_t i = 0; i < block->cfg.in_cnt; i++) {
        if ((block->cfg.in_connected >> i) & 0x01) {
            mem_access_s_t *access_node = (mem_access_s_t*)block->inputs[i];
            
            if (unlikely(!access_node)) {LOG_E(_TAG2, "NULL node");return false;}

            // Ensure context index is valid
            if (access_node->context_id >= 8) return false;
            emu_mem_t *mem = s_mem_contexts[access_node->context_id];
            
            if (unlikely(!mem)) return false; 

            emu_mem_instance_iter_t meta;
            meta.raw = (uint8_t*)mem->instances[access_node->target_type][access_node->instance_idx];
            
            if (unlikely(meta.raw == NULL)) {LOG_E(_TAG2, "NULL instance");return false;}
            //if any not updated return false
            if (meta.single->updated == 0) {return false;}
        }
    }
    LOG_D(_TAG2, "All inputs updated in block %d", block->cfg.block_idx);
    return true;
}

__attribute((always_inline)) static inline bool emu_block_check_input_updated(block_handle_t *block, uint8_t num){
    if (unlikely(!block || num >= block->cfg.in_cnt)) {return false;}
    if ((block->cfg.in_connected >> num) & 0x01) {
        mem_access_s_t *access_node = (mem_access_s_t*)block->inputs[num];
        if (unlikely(!access_node)) {return false;}
        emu_mem_t *mem = s_mem_contexts[access_node->context_id];
        if (unlikely(!mem)) return false; 
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)mem->instances[access_node->target_type][access_node->instance_idx];
        if (unlikely(meta.raw == NULL)) {LOG_E(_TAG2, "NULL instance");return false;}
        if (meta.single->updated == 0) {return false;}
        else{return true;}
    }
    return false;
}

__attribute((always_inline))static inline bool emu_check_updated(block_handle_t *block, uint8_t num) {
    if (unlikely(!block || num >= block->cfg.in_cnt)){ LOG_I("MEM", "nullptr"); return false;}

    if (!((block->cfg.in_connected >> num) & 0x01)) {LOG_I("MEM", "not conn"); return false;}

    mem_access_s_t *access_node = (mem_access_s_t*)block->inputs[num];
    if (unlikely(!access_node)) { LOG_I("MEM", "no node"); return false;}

    emu_mem_t *mem = s_mem_contexts[access_node->context_id]; 
    if (unlikely(!mem)) { LOG_I("MEM", "no mem ptr"); return false;} 
    
 
    void *raw_ptr = mem->instances[access_node->target_type][access_node->instance_idx];
    if (unlikely(!raw_ptr)) { LOG_I("MEM", "null inst"); return false;}

    emu_mem_instance_iter_t meta;
    meta.raw = (uint8_t*)raw_ptr;
    LOG_I("MEM", "DUMP block%d, %d, %d, %d, %d, %d",block->cfg.block_idx,  meta.single->dims_cnt, meta.single->context_id, meta.single->target_type, meta.single->start_idx, meta.single->updated);
    return meta.single->updated;
}

__attribute((always_inline)) static inline bool emu_block_check_and_get_en(block_handle_t *block, uint8_t num) {
    if (!emu_check_updated(block, num)) {return false;}
    bool en = false; 
    emu_err_t err = MEM_GET(&en, block->inputs[num]);
    LOG_I("CHECK IF EN", "EN: %d, err %s", en, EMU_ERR_TO_STR(err));
    return en;
}



__attribute((always_inline)) static inline bool emu_block_check_true_and_updated(block_handle_t *block, uint8_t num) {
   
    mem_access_s_t *acces = (mem_access_s_t*)(block->inputs[num]);
    emu_mem_t *mem = s_mem_contexts[acces->context_id];
    if (unlikely(!mem)) return false; 
    emu_mem_instance_iter_t instance;
    instance.raw = (uint8_t*)mem->instances[acces->target_type][acces->instance_idx];
    if (unlikely(!instance.raw)) return false;
    if(instance.single->updated == 1){
        bool en = 0; 
        MEM_GET(&en, block->inputs[num]);
        return en;
    }
    return false;
}


__attribute((always_inline)) static inline emu_result_t emu_block_set_output(block_handle_t *block, emu_variable_t *var, uint8_t num) {
    if (unlikely(!block || !var)) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_emu_block_set_output, block->cfg.block_idx, 0, "set output", "Null prt");
    }
    
    if (unlikely(num >= block->cfg.q_cnt)) {
        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_emu_block_set_output, block->cfg.block_idx, 0, "set output", "num exceeds total outs");
    }
    
    return mem_set(block->outputs[num], *var);
}

static inline void emu_block_reset_outputs_status(block_handle_t *block) {
    // 1. Szybka walidacja wejścia
    if (unlikely(!block || block->cfg.q_cnt == 0 || !block->outputs)) { return; }

    for (uint8_t i = 0; i < block->cfg.q_cnt; i++) {
        mem_access_s_t *access_node = (mem_access_s_t*)block->outputs[i];

        if (unlikely(!access_node)) continue;

        if(access_node->context_id == 0)continue;
        emu_mem_t *mem = s_mem_contexts[access_node->context_id];
        if (unlikely(!mem)) continue;
        
        uint16_t idx = access_node->instance_idx; 
        void *raw_inst = mem->instances[access_node->target_type][idx];

        if (likely(raw_inst)) {
            emu_mem_instance_iter_t meta;
            meta.raw = (uint8_t*)raw_inst;
            meta.single->updated = 0;
            LOG_I("MEM RESET", "DUMP block%d, %d, %d, %d, %d, %d",block->cfg.block_idx,  meta.single->dims_cnt, meta.single->context_id, meta.single->target_type, meta.single->start_idx, meta.single->updated);
        }
    }
}