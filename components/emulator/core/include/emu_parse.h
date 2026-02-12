#pragma once
#include "error_types.h"
#include "gatt_buff.h"
#include "order_types.h"
#include "emu_body.h"

typedef enum{
    PACKET_H_CONTEXT_CFG          = 0xF0,
    PACKET_H_INSTANCE             = 0xF1,
    PACKET_H_INSTANCE_SCALAR_DATA = 0xFA,
    PACKET_H_INSTANCE_ARR_DATA    = 0xFB,

    PACKET_H_LOOP_CFG             = 0xA0,
    PACKET_H_CODE_CFG             = 0xAA,

    PACKET_H_BLOCK_HEADER         = 0xB0,
    PACKET_H_BLOCK_INPUTS         = 0xB1,
    PACKET_H_BLOCK_OUTPUTS        = 0xB2,
    PACKET_H_BLOCK_DATA           = 0xBA,
}packet_header_t;

typedef emu_result_t (*emu_parse_func)(const uint8_t *packet_data, const uint16_t packet_len, void* custom);

extern emu_parse_func parse_dispatch_table[255];


/**
 * @brief Dispatch parser for given packet header
 * @param source message buffer with packets
 * @param header_to_parse header to parse from buffer
 * @param emu_code_handle_t code handle to provide to parsers that need it
 * @param extra_arg extra argument to provide to parser
 */
emu_result_t emu_parse_manager(chr_msg_buffer_t *source, emu_order_t order, emu_code_handle_t code_handle, void* extra_arg);



emu_result_t emu_parse_verify_code(emu_code_handle_t code);