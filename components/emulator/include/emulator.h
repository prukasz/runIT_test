#pragma once 
#include "stdint.h"
#include "gatt_buff.h"
#include "emulator_types.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHECK_PTR(ptr, name_str) ({                                  \
    if ((ptr) == NULL) {                                             \
        ESP_LOGE(TAG, "Null pointer: %s (function: %s)",             \
                 (name_str), __func__);                              \
        return EMU_ERR_NO_MEMORY;                                    \
    }                                                                \
})

emu_err_t emulator_parse_source_add(chr_msg_buffer_t * msg);

void emu(void* params);
