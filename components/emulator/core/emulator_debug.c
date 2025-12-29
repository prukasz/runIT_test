#include "gatt_buff.h"
#include "gatt_svc.h"
#include "emulator_debug.h"
#include "emulator_interface.h"
#include "emulator_types.h"
#include "esp_log.h"
#include "emulator_variables.h"

#define MAX_INDICATION_SIZE 500
static const char *TAG = "EMU_DEBUG";

chr_msg_buffer_t *output;


emu_result_t debug_blocks_value_dump(block_handle_t** block_struct_list, uint16_t total_block_cnt)
{
    emu_result_t res = {.code = EMU_OK};
    uint8_t *my_data;
    size_t my_data_size = 0;
    if (NULL==output){
        ESP_LOGW(TAG, "No output buffer provided for debug dump");
        res.code = EMU_ERR_NULL_PTR;
        res.warning = true;
        return res;   
    }
    
    for (uint16_t i = 0; i < total_block_cnt; i++) {
        block_handle_t *block = block_struct_list[i];
        // Add input data sizes
        for(uint8_t j = 0; j < block->in_cnt; j++){
            my_data_size += data_size(block->in_data_type_table[j]);
        }
        // Add output data sizes
        for(uint8_t j = 0; j < block->q_cnt; j++){
            my_data_size += data_size(block->q_data_type_table[j]);
        }
    }
    my_data_size += total_block_cnt * sizeof(uint16_t); //for block ids
    my_data = (uint8_t*)calloc(1, my_data_size);

    if(!my_data){
        ESP_LOGE(TAG, "No memory to allocate debug data buffer");
        res.code = EMU_ERR_NO_MEM;
        res.warning = true;
        return res;  
    }

    uint16_t idx = 0;
    for (uint16_t i = 0; i < total_block_cnt; i++) {
        block_handle_t *block = block_struct_list[i];
        memcpy(&my_data[idx], &block->block_idx, sizeof(uint16_t));
        idx += sizeof(uint16_t);
        for(uint8_t j = 0; j < block->in_cnt; j++){
            size_t data_sz = data_size(block->in_data_type_table[j]);
            memcpy(&my_data[idx], (uint8_t*)block->in_data + block->in_data_offsets[j], data_sz);
            idx += data_sz;
        }
        for(uint8_t j = 0; j < block->q_cnt; j++){
            size_t data_sz = data_size(block->q_data_type_table[j]);
            memcpy(&my_data[idx], (uint8_t*)block->q_data + block->q_data_offsets[j], data_sz);
            idx += data_sz;
        }
    }
    

    size_t total_len = my_data_size;
    size_t offset = 0;

    
    uint8_t *chunk_buffer = (uint8_t*)malloc(MAX_INDICATION_SIZE + 2);
    if (!chunk_buffer) {
        ESP_LOGE(TAG, "Failed to allocate chunk buffer");
        free(my_data);
        res.code = EMU_ERR_NO_MEM;
        return res;
    }
    
    while (offset < total_len) {
        size_t remaining = total_len - offset;
        size_t chunk_data_size = (remaining > MAX_INDICATION_SIZE) ? MAX_INDICATION_SIZE : remaining;
        
        if (offset == 0) {
            if (chunk_data_size > MAX_INDICATION_SIZE - 2) {
                chunk_data_size = MAX_INDICATION_SIZE - 2;
            }
            
            chunk_buffer[0] = (total_len >> 8) & 0xFF;
            chunk_buffer[1] = total_len & 0xFF;
            memcpy(chunk_buffer + 2, my_data, chunk_data_size);
            
            chr_msg_buffer_clear(output);
            chr_msg_buffer_add(output, chunk_buffer, chunk_data_size + 2);
            
            ESP_LOGI(TAG, "First chunk: total=%d, data=%d", total_len, chunk_data_size);
        } else {
            chr_msg_buffer_clear(output);
            chr_msg_buffer_add(output, my_data + offset, chunk_data_size);
            
            ESP_LOGI(TAG, "Chunk: offset=%d, size=%d", offset, chunk_data_size);
        }
        send_indication();
        offset += chunk_data_size;
        
    }
    
    ESP_LOGI(TAG, "All chunks sent successfully");
    free(chunk_buffer);
    free(my_data);

    return res;
    
}


emu_result_t emu_debug_output_add(chr_msg_buffer_t * msg){
    emu_result_t res = {.code = EMU_OK};
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        res.code = EMU_ERR_NULL_PTR;
        res.warning = true;
        return res;   
    }
    output = msg;
    return res;
}