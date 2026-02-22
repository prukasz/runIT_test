#include "emu_parse.h"
#include "emu_blocks.h"
#include "emu_body.h"
#include "emu_logging.h"
#include "emu_variables.h" 
#include "emu_variables_acces.h"
#include "emu_helpers.h"
#include "blocks_functions_list.h"
#include <string.h>
#include "emu_subscribe.h"
#include "emu_buffs.h"

static const char *TAG = __FILE_NAME__;

/**
 * @brief Parse PACKET_H_BLOCK_DATA packets and dispatch to block-specific parsers
 * 
 * Packet format: [header:u8][block_idx:u16][block_type:u8][packet_id:u8][data...]
 * Parser receives: [packet_id:u8][data...]
 * 
 * @param data Packet data after header byte (starts with block_idx)
 * @param packet_length Length of data
 * @param emu_code_handle Code handle containing block list
 */
#undef OWNER
#define OWNER EMU_OWNER_parse_block_data
static emu_result_t emu_block_parse_data(const uint8_t *data, const uint16_t packet_length, void *emu_code_handle) {
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    
    // Minimum packet: [block_idx:u16][block_type:u8][packet_id:u8] = 4 bytes
    if (packet_length < 4) {
        RET_E(EMU_ERR_PACKET_INCOMPLETE, "Block data packet too short: %d bytes", packet_length);
    }
    
    // Extract block_idx, block_type
    uint16_t block_idx = parse_get_u16(data, 0);
    uint8_t block_type = data[2];
    
    // Validate block index
    if (block_idx >= code->total_blocks) {
        RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Invalid block_idx %d (total: %d)", block_idx, code->total_blocks);
    }
    
    // Get block handle
    block_handle_t block = code->blocks_list[block_idx];
    if (!block) {
        RET_ED(EMU_ERR_NULL_PTR, block_idx, 0, "Block %d is NULL", block_idx);
    }
    
    // Verify block type matches
    if (block->cfg.block_type != block_type) {
        RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block_idx, 0, "Block type mismatch: expected %d, got %d", block->cfg.block_type, block_type);
    }
    
    // Get block-specific parser
    emu_block_parse_func parser = emu_block_parsers_table[block_type];
    if (!parser) {
        // Not an error - some blocks don't have parsers (e.g., BLOCK_SET)
        LOG_D(TAG, "No parser for block type %d (block_idx %d) - skipping", block_type, block_idx);
        return EMU_RESULT_OK();
    }
    
    // Call parser with stripped packet: [packet_id:u8][data...]
    const uint8_t *parser_payload = &data[3];  // Skip block_idx(2) + block_type(1)
    uint16_t parser_payload_len = packet_length - 3;
    
    emu_result_t res = parser(parser_payload, parser_payload_len, block);
    if (res.code != EMU_OK) {
        RET_ED(res.code, block_idx, ++res.depth, "Block parser failed for type %d", block_type);
    }
    
    return EMU_RESULT_OK();
}

static emu_parse_func parse_dispatch_table[255] = {
    [PACKET_H_CONTEXT_CFG]           = emu_mem_parse_create_context,
    [PACKET_H_INSTANCE]              = emu_mem_parse_instance_packet,
    [PACKET_H_INSTANCE_SCALAR_DATA]  = emu_mem_fill_instance_scalar, 
    [PACKET_H_INSTANCE_ARR_DATA]     = emu_mem_fill_instance_array,

    [PACKET_H_LOOP_CFG]              = NULL,
    [PACKET_H_CODE_CFG]              = emu_block_parse_create_list,

    [PACKET_H_BLOCK_HEADER]          = emu_block_parse_cfg,
    [PACKET_H_BLOCK_INPUTS]          = emu_block_parse_input, 
    [PACKET_H_BLOCK_OUTPUTS]         = emu_block_parse_output,
    [PACKET_H_BLOCK_DATA]            = emu_block_parse_data,
    [PACKET_H_SUBSCRIPTION_INIT]     = emu_subscribe_parse_init,
    [PACKET_H_SUBSCRIPTION_ADD]      = emu_subscribe_parse_register,
 };




emu_result_t emu_parse_manager(msg_packet_t *source, emu_order_t order, emu_code_handle_t code_handle, void* extra_arg){
    uint16_t packet_len = source->len-1; /*skip header*/
    uint8_t *packet_data = &source->data[1]; /*skip header*/

    return parse_dispatch_table[source->data[0]](packet_data, packet_len, code_handle);
}

#undef OWNER
#define OWNER EMU_OWNER_emu_parse_blocks_verify_all
emu_result_t emu_parse_verify_code(emu_code_handle_t code){

    if (!code) {
        RET_E(EMU_ERR_NULL_PTR,"Code handle is NULL");
    }
    if (!code->blocks_list) {
        RET_E(EMU_ERR_NULL_PTR,"blocks_list is NULL");
    }
    if (code->total_blocks == 0) {
        RET_W(EMU_ERR_BLOCK_INVALID_PARAM,"total_blocks is 0 — nothing to verify");
    }

    for (uint16_t i = 0; i < code->total_blocks; i++) {
        block_handle_t block = code->blocks_list[i];

        // ---- 1. Block must exist ----
        if (!block) {
            RET_ED(EMU_ERR_NULL_PTR, i, 0, "Block[%"PRIu16"] is NULL", i);
        }

        // ---- 2. Block must have a main function registered ----
        uint8_t btype = block->cfg.block_type;
        if (!blocks_main_functions_table[btype]) {
            RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, i, 0,
                   "Block[%"PRIu16"] type %u has no main function", i, btype);
        }

        // ---- 3. Verify connected inputs are not NULL ----
        uint16_t mask = block->cfg.in_connceted_mask;
        for (uint8_t in = 0; in < block->cfg.in_cnt; in++) {
            if (mask & (1u << in)) {
                // Input marked as connected — pointer must be valid
                if (!block->inputs) {
                    RET_ED(EMU_ERR_NULL_PTR, i, 0,
                           "Block[%"PRIu16"] inputs array is NULL", i);
                }
                if (!block->inputs[in]) {
                    RET_ED(EMU_ERR_NULL_PTR, i, 0,
                           "Block[%"PRIu16"] input[%u] marked connected but ptr is NULL", i, in);
                }
                if (!block->inputs[in]->instance) {
                    RET_ED(EMU_ERR_NULL_PTR, i, 0,
                           "Block[%"PRIu16"] input[%u] instance is NULL", i, in);
                }
            }
        }

        // ---- 4. Verify outputs are not NULL ----
        if (block->cfg.q_cnt > 0) {
            if (!block->outputs) {
                RET_ED(EMU_ERR_NULL_PTR, i, 0,
                       "Block[%"PRIu16"] outputs array is NULL (q_cnt=%u)", i, block->cfg.q_cnt);
            }
            for (uint8_t q = 0; q < block->cfg.q_cnt; q++) {
                if (!block->outputs[q]) {
                    RET_ED(EMU_ERR_NULL_PTR, i, 0,
                           "Block[%"PRIu16"] output[%u] ptr is NULL", i, q);
                }
                if (!block->outputs[q]->instance) {
                    RET_ED(EMU_ERR_NULL_PTR, i, 0,
                           "Block[%"PRIu16"] output[%u] instance is NULL", i, q);
                }
            }
        }

        // ---- 5. Dispatch block-specific verify if registered ----
        emu_block_verify_func verify_fn = emu_block_verify_table[btype];
        if (verify_fn) {
            emu_result_t res = verify_fn(block);
            if (res.code != EMU_OK) {
                RET_ED(res.code, i, ++res.depth,
                       "Block[%"PRIu16"] type %u verify failed", i, btype);
            }
        }

        LOG_I(TAG, "Block[%"PRIu16"] type %u — OK", i, btype);
    }

    LOG_I(TAG, "All %"PRIu16" blocks verified OK", code->total_blocks);
    return EMU_RESULT_OK();
}
