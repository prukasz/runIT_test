#pragma once
#include <stdint.h>

typedef struct{
    __attribute((packed)) struct {
        uint16_t block_idx;
        uint8_t block_type;  
        uint16_t in_connected;
        uint8_t in_cnt;
        uint8_t q_cnt;
    } cfg;
    void *custom_data;
    void **inputs;
    void **outputs;
}block_handle_t;


