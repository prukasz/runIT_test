#include "gatt_buff.h"
#include "esp_log.h"
#include "string.h"

/*Buffer to store received message or store messages to be sent via BLE*/

static const char *TAG = "CHR_MSG_BUFFER";

esp_err_t chr_msg_buffer_init(chr_msg_buffer_t *buf)
{
    if (!buf) return ESP_ERR_INVALID_ARG;
    buf->head = NULL;
    buf->count = 0;
    return ESP_OK;
}

esp_err_t chr_msg_buffer_add(chr_msg_buffer_t *buf, const uint8_t *msg, uint16_t len)
{
    if (!buf || !msg || len == 0) return ESP_ERR_INVALID_ARG;

    ble_msg_node_t *node = malloc(sizeof(ble_msg_node_t));
    if (!node) return ESP_ERR_NO_MEM;

    node->data = malloc(len);
    if (!node->data) {
        free(node);
        return ESP_ERR_NO_MEM;
    }

    memcpy(node->data, msg, len);
    node->len = len;
    node->next = NULL;

    if (!buf->head) {
        buf->head = node;
    } else {
        ble_msg_node_t *curr = buf->head;
        while (curr->next) curr = curr->next;
        curr->next = node;
    }

    buf->count++;
    ESP_LOGI(TAG, "Added msg len=%d (total=%zu)", len, buf->count);
    return ESP_OK;
}

esp_err_t chr_msg_buffer_get(chr_msg_buffer_t *buf, size_t index, uint8_t **msg_out, uint16_t *len_out)
{
    if (!buf || !msg_out || index >= buf->count) return ESP_ERR_INVALID_ARG;

    ble_msg_node_t *curr = buf->head;
    for (size_t i = 0; i < index; i++) {
        curr = curr->next;
    }

    *msg_out = curr->data;
    if (len_out) *len_out = curr->len;
    return ESP_OK;
}

size_t chr_msg_buffer_size(const chr_msg_buffer_t *buf)
{
    return buf ? buf->count : 0;
}

void chr_msg_buffer_clear(chr_msg_buffer_t *buf)
{
    if (!buf) return;
    ble_msg_node_t *curr = buf->head;
    while (curr) {
        ble_msg_node_t *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    buf->head = NULL;
    buf->count = 0;
    ESP_LOGI(TAG, "Message buffer cleared");
}
