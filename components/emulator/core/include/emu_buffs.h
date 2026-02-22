#pragma once
#include <stdint.h>
#include <stdlib.h>
#include "error_types.h"
#include "esp_err.h"

extern size_t mtu_size;

typedef struct msg_packet_t {
    uint8_t *data;
    size_t len;
} msg_packet_t;

extern msg_packet_t emu_in_msg_packet;
extern msg_packet_t emu_out_msg_packet;


/*
Unified slab allocator will be implemented later
*/

typedef struct{
    uint8_t* data;
    size_t pool_size;
    size_t next_address;
    size_t alignment;
}data_pool_t;

emu_err_t pool_create(data_pool_t* pool, size_t pool_size, size_t alignment);

emu_err_t pool_alloc(data_pool_t* pool, uint8_t** result, size_t size);

void pool_reset(data_pool_t* pool);

emu_err_t pool_destroy(data_pool_t* pool);

/* msg_packet buffer management */

esp_err_t emu_msg_buffs_init(size_t mtu);
msg_packet_t* emu_get_in_msg_packet(void);
msg_packet_t* emu_get_out_msg_packet(void);
size_t emu_get_mtu_size(void);




