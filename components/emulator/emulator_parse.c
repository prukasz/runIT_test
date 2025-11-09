#include "emulator_parse.h"
#include "emulator.h"
#include "emulator_variables.h"
#include "esp_log.h"

static const char *TAG = "EMU_PARSE";
#define PACKET_MEM_SIZE 11
#define HEADER_SIZE 2

bool _check_arr_header(uint8_t *data, uint8_t *step)
{
    uint16_t header = ((data[0] << 8) | data[1]);
    if (header == EMU_H_VAR_ARR_1D)
    {
        *step = 2;
        return true;
    } // type, dim1
    if (header == EMU_H_VAR_ARR_2D)
    {
        *step = 3;
        return true;
    } // type, dim1, dim2
    if (header == EMU_H_VAR_ARR_3D)
    {
        *step = 4;
        return true;
    } // type, dim1, dim2, dim3
    return false;
}

/*check if header match desired*/
bool _check_header(uint8_t *data, emu_header_t header)
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

emu_err_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem)
{
    uint8_t emu_mem_size_single[9];
    uint8_t *data;
    uint16_t len;
    int start_index = -1;
    size_t buff_size = chr_msg_buffer_size(source);

    // parse single variables
    for (size_t i = 0; i < buff_size; ++i)
    {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len == 2 && _check_header(data, EMU_H_VAR_ORD))
        {
            if ((i + 1) < buff_size)
            {
                chr_msg_buffer_get(source, (i + 1), &data, &len);
                if (len == PACKET_MEM_SIZE && _check_header(data, EMU_H_VAR_START))
                {
                    ESP_LOGI(TAG, "Found sizes of variables at index %d", i+1);
                    start_index = i + 1;
                    memcpy(emu_mem_size_single, &data[HEADER_SIZE], (PACKET_MEM_SIZE - HEADER_SIZE));
                    for (int i = 0; i < PACKET_MEM_SIZE; ++i)
                    {
                        ESP_LOGD(TAG, "type %d: count %d", i, emu_mem_size_single[i]);
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
    emu_err_t err = emu_variables_create(mem, emu_mem_size_single);
    err = emu_arrays_create(source, mem, start_index);
    return err;
}

#define GET_2_BYTES(data)   ((uint16_t)(((data[3]) << 8) | (data[4])))

emu_err_t emu_parse_into_variables(chr_msg_buffer_t *source, emu_mem_t *mem) {
    uint8_t *data;
    uint16_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len > HEADER_SIZE) {
            uint8_t arr_index = (uint8_t)data[2];
            uint16_t offset   = (uint16_t)GET_2_BYTES(data);
            size_t bytes_to_copy = len - 5;

            switch ((emu_header_t)((data[0] << 8) | data[1])) {

                case EMU_H_VAR_DATA_0: { // uint8_t arrays
                    arr_ui8_t *arr = &mem->arr_ui8[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(uint8_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (uint8_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_1: { // uint16_t arrays
                    arr_ui16_t *arr = &mem->arr_ui16[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(uint16_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (uint16_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_2: { // uint32_t arrays
                    arr_ui32_t *arr = &mem->arr_ui32[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(uint32_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (uint32_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_3: { // int8_t arrays
                    arr_i8_t *arr = &mem->arr_i8[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(int8_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (int8_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_4: { // int16_t arrays
                    arr_i16_t *arr = &mem->arr_i16[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(int16_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (int16_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_5: { // int32_t arrays
                    arr_i32_t *arr = &mem->arr_i32[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(int32_t);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (int32_t table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_6: { // float arrays
                    arr_f_t *arr = &mem->arr_f[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(float);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (float table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_7: { // double arrays
                    arr_d_t *arr = &mem->arr_d[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(double);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (double table %d)", arr_index);
                    }
                    break;
                }

                case EMU_H_VAR_DATA_8: { // bool arrays
                    arr_b_t *arr = &mem->arr_b[arr_index];
                    size_t total_size = arr->dims[0];
                    if (arr->num_dims > 1) total_size *= arr->dims[1];
                    if (arr->num_dims > 2) total_size *= arr->dims[2];
                    total_size *= sizeof(bool);

                    if ((offset + bytes_to_copy) <= total_size) {
                        memcpy(&arr->data[offset], &data[5], bytes_to_copy);
                    } else {
                        ESP_LOGW("EMU_PARSE", "Write out of bounds (bool table %d)", arr_index);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
    return EMU_OK;
}


