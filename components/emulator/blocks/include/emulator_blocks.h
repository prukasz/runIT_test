#pragma once
#include "emulator_types.h"
#include "emulator_interface.h"
#include "emulator_variables.h"
#include "emulator_logging.h"
#include "emulator_body.h"
#include "emulator_variables_acces.h"

/**
 * @brief Function pointer for block code execution
 */
typedef emu_result_t (*emu_block_func)(block_handle_t *block);
/**
 * @brief Function pointer for block parser
 */
typedef emu_result_t (*emu_block_parse_func)(chr_msg_buffer_t *source, block_handle_t *block);
/**
 * @brief Function pointer for block free function
 */
typedef void (*emu_block_free_func)(block_handle_t *block);
/**
 * @brief Function pointer for block verification function
 */
typedef emu_result_t (*emu_block_verify_func)(block_handle_t *block);

/**
 * @brief Reset all blocks in list
 */
void emu_blocks_free_all(emu_code_handle_t code); 

/**
 * @brief Get packet with total block count and create table
 */
emu_result_t emu_parse_blocks_total_cnt(chr_msg_buffer_t *source, emu_code_handle_t code);

/**
 * @brief Parse blocks/block and it's content using provided functions
 */
emu_result_t emu_parse_block(chr_msg_buffer_t *source, emu_code_handle_t code);
   
/**
 * @brief Verify all blocks in code for integrity and correctness
 * This checks if all blocks are properly configured and linked
 * It is recommended to run this after parsing and before executing the code
 * @return emu_result_t Result of verification
 * @note This function uses block specific verify functions from "emu_block_verify_table" 
 * please ensure those are properly implemented for each block type if blocks create custom data in parse step
 */
emu_result_t emu_parse_blocks_verify_all(emu_code_handle_t code);


/**
 * @brief Parse inputs for single block from message buffer 
 * @return emu_result_t Result of parsing
 * @note This function is used internally by emu_parse_block but can be used externally if needed
 * This autodetects inputs based on block index and input count and packet headers
 */
emu_result_t emu_parse_block_inputs(chr_msg_buffer_t *source, block_handle_t *block);

/**
 * @brief Parse outputs for single block from message buffer 
 * @return emu_result_t Result of parsing
 * @note This function is used internally by emu_parse_block but can be used externally if needed
 * This autodetects outputs based on block index and output count and packet headers
 */
emu_result_t emu_parse_block_outputs(chr_msg_buffer_t *source, block_handle_t *block);

/****************************************************************************************************************
 * Helper inline functions for blocks operations
 * They are optional but help to keep code clean and readable
 * Please use them where possible 
 * "block_check_EN" can be also used to get BOOL input values but then requires input to be updated (always)
 * All those functions assumes that code is complete and there are no NULL pointers / some are checked but not all
 * NOTE: 
 * Please run emu_parse_blocks_verify_all before using those functions to ensure code integrity when code crashes
 ***************************************************************************************************************/

/**
 * @brief Check if all inputs in block updated (this is required for most blocks to run (like in PLC) / behaves like "IF PREV EXECUTED I CAN RUN")
 * @note This checks instances of blocks outputs in context memory (s_mem_contexts)[1] only
 */
static __always_inline bool emu_block_check_inputs_updated(block_handle_t *block) {

    for (uint8_t i = 0; i < block->cfg.in_cnt; i++) {
        // Sprawdzenie bitowe czy wejście jest podłączone
        if ((block->cfg.in_connected >> i) & 0x01) {
            
            mem_access_s_t *access_node = (mem_access_s_t*)block->inputs[i];
            
            // Safety check dla access node
            if (unlikely(!access_node)) { return false; }

            // ZAMIANA: Bezpośredni wskaźnik do instancji
            // Wcześniej: s_mem_contexts[id]->instances[type][idx] (wolne!)
            // Teraz: access_node->instance (szybkie!)
            emu_mem_instance_s_t *instance = (emu_mem_instance_s_t*)access_node->instance;
            
            // Safety check dla instancji
            if (unlikely(!instance)) { return false; }

            // Logika: Jeśli KTÓREKOLWIEK podłączone wejście nie jest zaktualizowane -> Stop
            if (instance->updated == 0) { return false; }
        }
    }
    // Jeśli przeszliśmy pętlę, znaczy że wszystkie podłączone wejścia są 'updated'
    return true;
}


/**
 * @brief Check if specific input in block is updated (look into mem instance updated flag)
 */
static __always_inline bool block_in_updated(block_handle_t *block, uint8_t num) {
    // 1. Sprawdzenie czy numer wejścia mieści się w zakresie
    if (unlikely(num >= block->cfg.in_cnt)) { return false; }

    // 2. Sprawdzenie czy wejście jest fizycznie podłączone (maska bitowa)
    if (!((block->cfg.in_connected >> num) & 0x01)) { return false; }

    // 3. Pobranie węzła dostępu
    mem_access_s_t *access_node = (mem_access_s_t*)block->inputs[num];
    if (unlikely(!access_node)) { return false; }

    // 4. Pobranie instancji BEZPOŚREDNIO ze wskaźnika
    // ZAMIANA: Usuwamy s_mem_contexts[...]. access_node->instance wskazuje prosto na metadane.
    emu_mem_instance_s_t *instance = (emu_mem_instance_s_t*)access_node->instance;

    // 5. Sprawdzenie i zwrot flagi
    if (likely(instance)) {
        return instance->updated;
    }

    return false;
}


/**
 * @brief This wrapper is made to eaisly check is input updated and is value true (EN conditions)
*/
static __always_inline bool block_check_EN(block_handle_t *block, uint8_t num) {
    if (!block_in_updated(block, num)) {return false;}
    bool en = false; 
    emu_result_t err = MEM_GET(&en, block->inputs[num]);

    //we report error but still return the value (false if error occurs)
    if (unlikely(err.code != EMU_OK)) {
        EMU_REPORT_ERROR_WARN(err.code, EMU_OWNER_block_check_EN, block->cfg.block_idx, 1,  "block_check_EN", "Failed to get EN value block %d", block->cfg.block_idx);
    }
    return en;
}


/**
 * @brief Set output value in block (wrapper around mem_set just to clarify usage)
 */
static __always_inline emu_result_t block_set_output(block_handle_t *block, emu_variable_t *var, uint8_t num) {
    //check if output can even exist
    if (unlikely(num >= block->cfg.q_cnt)) {
        EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_set_output, block->cfg.block_idx, 0, "set output", "num exceeds total outs");
    }
    //as said just wrapper around mem_set
    return mem_set(block->outputs[num], *var);
}

/**
 * @brief Reset all outputs "updated" status in block (for context 1 only)
 */
static __always_inline void emu_block_reset_outputs_status(block_handle_t *block) {
    // Szybkie wyjście
    if (unlikely(block->cfg.q_cnt == 0)) { return; }

    for (uint8_t i = 0; i < block->cfg.q_cnt; i++) {
        mem_access_s_t *access_node = (mem_access_s_t*)block->outputs[i];
        
        // Safety checks
        if (unlikely(!access_node)) continue;

        // 1. Pobieramy instancję bezpośrednio ze wskaźnika
        // (Brak kosztownego szukania w globalnych tablicach s_mem_contexts)
        emu_mem_instance_s_t *instance = (emu_mem_instance_s_t*)access_node->instance;

        if (unlikely(!instance)) continue;

        // 2. Sprawdzamy Context ID wewnątrz metadanych instancji
        // Hardcoded 1 for block outputs context check
        if (instance->context_id != 1) continue;
        
        // 3. Reset flagi
        instance->updated = 0;
    }
}