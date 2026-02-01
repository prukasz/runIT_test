#pragma once
#include "stdint.h"
#include "esp_err.h"

typedef struct ble_msg_node {
    uint8_t *data;
    uint16_t len;
    struct ble_msg_node *next;
} ble_msg_node_t;

typedef struct{
    ble_msg_node_t *head;
    size_t count;
}chr_msg_buffer_t;

esp_err_t chr_msg_buffer_init(chr_msg_buffer_t *buf);
esp_err_t chr_msg_buffer_add(chr_msg_buffer_t *buf, const uint8_t *msg, size_t len);
esp_err_t chr_msg_buffer_get(chr_msg_buffer_t *buf, size_t index, uint8_t **msg_out, size_t *len_out);
size_t chr_msg_buffer_size(const chr_msg_buffer_t *buf);
void chr_msg_buffer_clear(chr_msg_buffer_t *buf);
