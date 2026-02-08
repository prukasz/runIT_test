#include "emu_parse.h"
#include "emu_blocks.h"
#include "emu_body.h"
#include "emu_types.h"
#include "emu_logging.h"
#include "emu_variables.h" 
#include "emu_variables_acces.h"
#include "emu_helpers.h"
#include "blocks_functions_list.h"
#include <string.h>

static const char *TAG = __FILE_NAME__;

/**
 * @brief Parse PACKET_H_BLOCK_DATA packets and dispatch to block-specific parsers
 * 
 * Packet format: [header:u8][block_idx:u16][block_type:u8][packet_id:u8][data...]
 * Parser receives: [packet_id:u8][data...]
 * 
 * @param data Packet data after header byte (starts with block_idx)
 * @param data_len Length of data
 * @param emu_code_handle Code handle containing block list
 */
#undef OWNER
#define OWNER EMU_OWNER_parse_block_data
static emu_result_t emu_block_parse_data(const uint8_t *data, const uint16_t data_len, void *emu_code_handle) {
    emu_code_handle_t code = (emu_code_handle_t)emu_code_handle;
    
    // Minimum packet: [block_idx:u16][block_type:u8][packet_id:u8] = 4 bytes
    if (data_len < 4) {
        RET_E(EMU_ERR_PACKET_INCOMPLETE, "Block data packet too short: %d bytes", data_len);
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
    uint16_t parser_payload_len = data_len - 3;
    
    emu_result_t res = parser(parser_payload, parser_payload_len, block);
    if (res.code != EMU_OK) {
        RET_ED(res.code, block_idx, ++res.depth, "Block parser failed for type %d", block_type);
    }
    
    return EMU_RESULT_OK();
}

emu_parse_func parse_dispatch_table[255] = {
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
 };

emu_result_t parse_dispatch(chr_msg_buffer_t *source, packet_header_t header_to_parse, void* extra_arg){
    uint16_t buff_len = chr_msg_buffer_size(source);
    size_t packet_len = 0; 
    uint8_t *packet_data;
    emu_result_t res = EMU_RESULT_OK();
    LOG_I(TAG, "Dispatching parse for header 0x%02X over %d packets", header_to_parse, buff_len);
    for(uint16_t current_packet_idx = 0; current_packet_idx < buff_len; current_packet_idx++){
        chr_msg_buffer_get(source, current_packet_idx, &packet_data, &packet_len);
        if(!parse_check_header(packet_data, packet_len, header_to_parse) || packet_len <=2){continue;}
        //we provide extra_arg to parsers that require them 
        parse_dispatch_table[header_to_parse](&packet_data[1], packet_len-1, extra_arg);
    }
    return res;
}

// Define bit masks for each parsing step

#define STATUS_CREATED_CONTEXT   BIT(31) // Step 1
#define STATUS_PARSED_VARS       BIT(30) // Step 2
#define STATUS_FILLED_VARS       BIT(29) // Step 3
#define STATUS_CREATED_BLOCKS    BIT(28) // Step 4
#define STATUS_CONFIG_BLOCKS     BIT(27) // Step 5
#define STATUS_FILLED_BLOCKS     BIT(26) // Step 6
#define STATUS_CREATED_LOOP      BIT(25) // Step 7

/**
 * @brief Guard macro to ensure correct order of parsing steps
 */
#define EMU_GUARD_ORDER(_bit) ({ \
    __typeof__(_bit) _b = (_bit); \
    uint32_t _required = ~((uint32_t)_b | ((uint32_t)_b - 1)); \
    if (parse_status != _required) { \
        emu_result_t res; \
        res.code = EMU_ERR_SEQUENCE_VIOLATION; \
        return res;\
    } \
})

emu_result_t emu_parse_manager(chr_msg_buffer_t *source, emu_order_t order, emu_code_handle_t code_handle, void* extra_arg){
    // Static state variable preserves status between function calls
    static uint32_t parse_status = 0;
    
    // Default result
    emu_result_t res = EMU_RESULT_OK();

    switch (order) {
        // --- STEP 1: CREATE CONTEXT ---
        case ORD_PARSE_CONTEXT_CFG:
            // Don't guard - allow multiple contexts
            res = parse_dispatch(source, PACKET_H_CONTEXT_CFG, NULL);
            parse_status |= STATUS_CREATED_CONTEXT;
            break;
        case ORD_PARSE_VARIABLES:
            // Don't guard - variables can be created for each context
            res = parse_dispatch(source, PACKET_H_INSTANCE, NULL);
            parse_status |= STATUS_PARSED_VARS;
            break;
        case ORD_PARSE_VARIABLES_S_DATA:
            // Don't guard - data can be filled for each context
            res = parse_dispatch(source, PACKET_H_INSTANCE_SCALAR_DATA, NULL);
            parse_status |= STATUS_FILLED_VARS;
            break;
        case ORD_PARSE_VARIABLES_ARR_DATA:
            res = parse_dispatch(source, PACKET_H_INSTANCE_ARR_DATA, NULL);
            parse_status |= STATUS_FILLED_VARS;
            break;

        
        case ORD_PARSE_CODE_CFG:
            EMU_GUARD_ORDER(STATUS_CREATED_BLOCKS);
            LOG_I(TAG, "Parsing code configuration (blocks list)");
            res = parse_dispatch(source, PACKET_H_CODE_CFG, code_handle);
            parse_status |= STATUS_CREATED_BLOCKS;
            break;

        case ORD_PARSE_BLOCK_HEADER:
            EMU_GUARD_ORDER(STATUS_CONFIG_BLOCKS);
            LOG_I(TAG, "Parsing block headers");
            res = parse_dispatch(source, PACKET_H_BLOCK_HEADER, code_handle);
            parse_status |= STATUS_CONFIG_BLOCKS;
            break;
        
        case ORD_PARSE_BLOCK_INPUTS:
            //EMU_GUARD_ORDER(STATUS_CONFIG_BLOCKS);
            LOG_I(TAG, "Parsing block inputs");
            res = parse_dispatch(source, PACKET_H_BLOCK_INPUTS, code_handle);
            parse_status |= STATUS_CONFIG_BLOCKS;
            break;
    
        case ORD_PARSE_BLOCK_OUTPUTS:
            //EMU_GUARD_ORDER(STATUS_CONFIG_BLOCKS);
            LOG_I(TAG, "Parsing block outputs");
            res = parse_dispatch(source, PACKET_H_BLOCK_OUTPUTS, code_handle);
            parse_status |= STATUS_CONFIG_BLOCKS;
            break;
        
        case ORD_PARSE_BLOCK_DATA:
            EMU_GUARD_ORDER(STATUS_FILLED_BLOCKS);
            LOG_I(TAG, "Parsing block data");
            res = parse_dispatch(source, PACKET_H_BLOCK_DATA, code_handle);
            parse_status |= STATUS_FILLED_BLOCKS;
            break;

        case ORD_PARSE_LOOP_CFG:
            EMU_GUARD_ORDER(STATUS_CREATED_LOOP);
            res = parse_dispatch(source, PACKET_H_LOOP_CFG, code_handle);
            parse_status |= STATUS_CREATED_LOOP;
            break;

        case ORD_PARSE_RESET_STATUS:
            parse_status = 0;
            break;
        default:
        break;
    }

    return res;
}

#undef OWNER
#define OWNER EMU_OWNER_emu_parse_blocks_verify_all
emu_result_t emu_parse_verify_code(emu_code_handle_t code){

    if (!code) {
        RET_ED(EMU_ERR_NULL_PTR, 0, 0, "Code handle is NULL");
    }
    if (!code->blocks_list) {
        RET_ED(EMU_ERR_NULL_PTR, 0, 0, "blocks_list is NULL");
    }
    if (code->total_blocks == 0) {
        RET_WD(EMU_ERR_BLOCK_INVALID_PARAM, 0, 0, "total_blocks is 0 — nothing to verify");
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
