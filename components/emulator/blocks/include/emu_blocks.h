#pragma once
#include "emu_types.h"
#include "emu_interface.h"
#include "emu_variables.h"
#include "emu_logging.h"
#include "emu_body.h"
#include "emu_variables_acces.h"

/**
* @brief Block type identification code
*/
typedef enum{
    BLOCK_MATH = 0x01,
    BLOCK_SET = 0x02,
    BLOCK_LOGIC = 0x03,
    BLOCK_COUNTER = 0x04,
    BLOCK_CLOCK = 0x05,
    BLOCK_FOR = 0x08,
    BLOCK_TIMER = 0x09,
    BLOCK_IN_SELECTOR = 0x0A,
    BLOCK_Q_SELECTOR = 0x0B,
    BLOCK_LATCH = 0x0C,
}block_type_t;


/**
 * @brief Block data packet IDs - identifies type of data in PACKET_H_BLOCK_DATA packets
 * @note Packet format after header: [block_idx:u16][block_type:u8][packet_id:u8][data...]
 */
typedef enum {
    // Data packets (0x00-0x0F)
    BLOCK_PKT_CONSTANTS      = 0x00,  // Constants: [cnt:u8][float...]
    BLOCK_PKT_CFG            = 0x01,  // Configuration: block-specific config data
    
    // Code/expression packets (0x10-0x1F)
    BLOCK_PKT_INSTRUCTIONS   = 0x10,  // Instructions: bytecode for Math, Logic blocks
    
    // Dynamic option packets (0x20+)
    BLOCK_PKT_OPTIONS_BASE   = 0x20,  // Selector options: 0x20 + option_index
} block_packet_id_t;

/**
 * @brief Function pointer for block code execution
 */
typedef emu_result_t (*emu_block_func)(block_handle_t block);
/**
 * @brief Function pointer for block parser
 * @param packet_data Raw packet data (block_idx/block_type already stripped by dispatcher)
 * @param packet_len Length of packet data
 * @param block Block handle to configure
 * @note Packet format: [packet_id:u8][data...]
 */
typedef emu_result_t (*emu_block_parse_func)(const uint8_t *packet_data, const uint16_t packet_len, void *block);
/**
 * @brief Function pointer for block free function
 */
typedef void (*emu_block_free_func)(block_handle_t block);
/**
 * @brief Function pointer for block verification function
 */
typedef emu_result_t (*emu_block_verify_func)(block_handle_t block);

/**
 * @brief Parse and create list for all blocks
 * @param data packet buff (skip header)
 * @param el_cnt packet buff len (-header len)
 * @param emu_code_handle code handle where space will be created 
 * @note Packet [uint16_t blocks_count]
 */
emu_result_t emu_block_parse_create_list(const uint8_t *data, const uint16_t el_cnt, void * emu_code_handle);

/**
 * @brief Parse and create block with config
 * @param data packet buff (skip header)
 * @param el_cnt packet buff len (-header len)
 * @param emu_code_handle code handle where block will be created
 * @note Packet block_data_t.cfg structure (packed)
 */ 
emu_result_t emu_block_parse_cfg(const uint8_t *data, const uint16_t el_cnt, void * emu_code_handle);

/**
 * @brief Parse and assign block input
 * @param data packet buff (skip header)
 * @param el_cnt packet buff len (-header len)
 * @param emu_code_handle code handle where block input will be assinged
 * @note Packet [uint16_t block_idx] + [uint8_t in_idx] + mem_access_t parse packet 
 */
emu_result_t emu_block_parse_input(const uint8_t *data, const uint16_t el_cnt, void * emu_code_handle);

/**
 * @brief Parse and assign block output 
 * @param data packet buff (skip header)
 * @param el_cnt packet buff len (-header len)
 * @param emu_code_handle code handle where block output will be assinged
 * @note Packet [uint16_t block_idx] + [uint8_t q_idx] + mem_access_t parse packet 
 */
emu_result_t emu_block_parse_output(const uint8_t *data, const uint16_t el_cnt, void * emu_code_handle);

/**
 * @brief Free all blocks in code handle
 */
void emu_blocks_free_all(void* emu_code_handle);


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
static __always_inline bool emu_block_check_inputs_updated(block_handle_t block) {
    uint16_t mask = block->cfg.in_connceted_mask;

    for (uint8_t i = 0; i < block->cfg.in_cnt; i++) {
        if ((mask & 1) && !block->inputs[i]->instance->updated) {return false;}
        mask >>= 1;
    }
    return true;
}


/**
 * @brief Check if specific input in block is updated (look into mem instance updated flag)
 */
static __always_inline bool block_in_updated(block_handle_t block, uint8_t num) {
    if (unlikely(num >= block->cfg.in_cnt)) { return false; }
    return (((block->cfg.in_connceted_mask>>num) & 1) && block->inputs[num]->instance->updated);
}


/**
 * @brief This wrapper is made to eaisly check is input updated and is value true (EN conditions)
*/
static __always_inline bool block_check_in_true(block_handle_t block, uint8_t num) {
    if (!block_in_updated(block, num)) {return false;}
    bool en = false; 
    emu_result_t err = MEM_GET(&en, block->inputs[num]);

    //we report error but still return the value (false if error occurs)
    if (unlikely(err.code != EMU_OK)) {
        EMU_REPORT_ERROR_WARN(err.code, EMU_OWNER_block_check_in_true, block->cfg.block_idx, ++err.depth,  "block_check_in_true", "Failed to get EN value block %d, error %s", block->cfg.block_idx, EMU_ERR_TO_STR(err.code));
    }
    return en;
}


/**
 * @brief Set output value in block (wrapper around mem_set just to clarify usage)
 */
static __always_inline emu_result_t block_set_output(block_handle_t block, mem_var_t var, uint8_t num) {
    //check if output can even exist
    if (unlikely(num >= block->cfg.q_cnt)) {EMU_RETURN_CRITICAL(EMU_ERR_BLOCK_INVALID_PARAM, EMU_OWNER_block_set_output, block->cfg.block_idx, 0, "block_set_output", "num exceeds total outs");}
    //as said just wrapper around mem_set
    return mem_set(var, block->outputs[num]);
}

/**
 * @brief Reset all outputs "updated" status in block (for context 1 only)
 */
static __always_inline void emu_block_reset_outputs_status(block_handle_t block) {
    //for no output block
    if (unlikely(block->cfg.q_cnt == 0)) { return; }

    for (uint8_t i = 0; i < block->cfg.q_cnt; i++) {
        //we can clear only if instance says so 
        if (block->outputs[i]->instance->can_clear) {
            block->outputs[i]->instance->updated = 0;
        }
    }
}
