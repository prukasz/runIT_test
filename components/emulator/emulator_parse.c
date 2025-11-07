#include "emulator_parse.h"
#include "emulator.h"
#include "emulator_variables.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

#define PACKET_MEM_SIZE 13
#define HEADER_SIZE 2

static const char *TAG = "EMU_PARSE";

bool _check_arr_header(uint8_t *data, uint8_t *step)
{
    uint16_t header = ((data[0] << 8) | data[1]);
    if (header == 0xFF01)
    {
        *step = 2;
        return true;
    } // type, dim1
    if (header == 0xFF02)
    {
        *step = 3;
        return true;
    } // type, dim1, dim2
    if (header == 0xFF03)
    {
        *step = 4;
        return true;
    } // type, dim1, dim2, dim3
    return false;
}

/*check if header match desired*/
bool _check_header(uint8_t *data, emu_order_t header)
{
    return ((data[0] << 8) | data[1]) == header;
}

/*check if packet includes all sizes for arrays*/
bool _check_arr_packet_size(uint16_t len, uint8_t step)
{
    if (len <= HEADER_SIZE)
    {
        return false;
    }
    return (len - HEADER_SIZE) % step == 0;
}

emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem, uint8_t *emu_mem_size)
{
    uint8_t *data;
    uint16_t len;
    int start_index = -1;
    size_t buff_size = chr_msg_buffer_size(source);

    // parse single variables
    for (size_t i = 0; i < buff_size; ++i)
    {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len == 2 && _check_header(data, ORD_START_BYTES))
        {

            if ((i + 1) < buff_size)
            {
                chr_msg_buffer_get(source, (i + 1), &data, &len);
                if (len == PACKET_MEM_SIZE && _check_header(data, ORD_H_VARIABLES))
                {
                    ESP_LOGI(TAG, "Found sizes of variables at index %d", i);
                    start_index = i + 1;
                    memcpy(emu_mem_size, &data[HEADER_SIZE], (PACKET_MEM_SIZE - HEADER_SIZE));
                    for (int i = 0; i < PACKET_MEM_SIZE; ++i)
                    {
                        ESP_LOGD(TAG, "type %d: count %d", i, emu_mem_size[i]);
                    }
                    break;
                }
            }
        }
    }
    if (start_index == -1)
    {
        ESP_LOGE(TAG, "Varaibles sizes not found");
        return EMU_ERR_INVALID_DATA;
    }
    emu_variables_create(mem, emu_mem_size);
    emu_arrays_create(source, mem, start_index);

    return EMU_OK;
}
