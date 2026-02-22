#include "emu_buffs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = __FILE_NAME__;

/* ---- msg_packet globals ---- */
size_t mtu_size = 0;
msg_packet_t emu_in_msg_packet = {0};
msg_packet_t emu_out_msg_packet = {0};

#define IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))


emu_err_t pool_create(data_pool_t* pool, size_t pool_size, size_t alignment) {
    if (!pool || pool_size == 0) return EMU_ERR_INVALID_ARG;

    if (!IS_POWER_OF_TWO(alignment)) return EMU_ERR_INVALID_ARG;

   
    size_t aligned_size = (pool_size + alignment - 1) & ~(alignment - 1);

    pool->data = (uint8_t*)aligned_alloc(alignment, aligned_size);
    if (!pool->data) return EMU_ERR_NO_MEM;

    memset(pool->data, 0, aligned_size);

    pool->pool_size = aligned_size;
    pool->alignment = alignment;
    pool->next_address = 0;

    return EMU_OK;
}

emu_err_t pool_alloc(data_pool_t* pool, uint8_t** result, size_t size) {
    if (!pool || !result || size == 0) return EMU_ERR_INVALID_ARG;

    size_t align = (size_t)pool->alignment;
    size_t aligned_size = (size + align - 1) & ~(align - 1);

    if (aligned_size > pool->pool_size - pool->next_address) {
        *result = NULL;
        return EMU_ERR_NO_MEM;
    }

    *result = pool->data + pool->next_address;
    pool->next_address += aligned_size;

    return EMU_OK;
}

void pool_reset(data_pool_t* pool) {
    if (!pool) return;
    pool->next_address = 0;
    memset(pool->data, 0, pool->pool_size); 
}

emu_err_t pool_destroy(data_pool_t* pool) {
    if (!pool) return EMU_ERR_INVALID_ARG;

    free(pool->data);
    pool->data = NULL;
    pool->pool_size = 0;
    pool->next_address = 0;
    pool->alignment = 0;
    
    return EMU_OK;
}

/* ---- msg_packet helpers ---- */

esp_err_t emu_msg_buffs_init(size_t mtu){
    mtu_size = mtu;
    if(emu_in_msg_packet.data || emu_out_msg_packet.data){
        free(emu_in_msg_packet.data);
        free(emu_out_msg_packet.data);
    }

    emu_in_msg_packet.data = malloc(mtu_size);
    if (!emu_in_msg_packet.data) {return ESP_ERR_NO_MEM;}
    emu_out_msg_packet.data = malloc(mtu_size);
    if (!emu_out_msg_packet.data) {
        free(emu_in_msg_packet.data);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Message buffers initialized with MTU size: %zu bytes", mtu_size);
    return ESP_OK;
}

msg_packet_t* emu_get_in_msg_packet(void){
    return &emu_in_msg_packet;
}

msg_packet_t* emu_get_out_msg_packet(void){
    return &emu_out_msg_packet;
}

size_t emu_get_mtu_size(void){
    return mtu_size;
}