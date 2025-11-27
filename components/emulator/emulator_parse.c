#include "emulator_parse.h"
#include "emulator_blocks.h"
#include "emulator_block_utils.h"

static const char *TAG = "EMU_PARSE";
#define PACKET_SINGLE_VAR_SIZE 11  //2 + 9 * type cnt
#define HEADER_SIZE 2
static void _parse_assign_fuction(uint8_t block_type, uint16_t block_id);

/*********************ARR cheks*****************/
#define STEP_ARR_1D 2 // type, dim1
#define STEP_ARR_2D 3 // type, dim1, dim2
#define STEP_ARR_3D 4 // type, dim1, dim2, dim3
bool _check_arr_header(uint8_t *data, uint8_t *step)
{
    uint16_t header = ((data[0] << 8) | data[1]);
    if (header == EMU_H_VAR_ARR_1D)
    {
        *step = STEP_ARR_1D;
        return true;
    } 
    if (header == EMU_H_VAR_ARR_2D)
    {
        *step = STEP_ARR_2D;
        return true;
    } 
    if (header == EMU_H_VAR_ARR_3D)  
    {
        *step = STEP_ARR_3D;
        return true;
    }
    return false;
}
bool _check_header(uint8_t *data, emu_header_t header)
{   
    return ((data[0] << 8) | data[1]) == header;
}
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
    uint8_t *data;
    uint16_t len;
    int start_index = -1;
    uint16_t buff_size = chr_msg_buffer_size(source);

    // parse single variables
    for (uint16_t i = 0; i < buff_size; ++i)
    {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len == 2 && _check_header(data, EMU_H_VAR_ORD))
        {
            if ((i + 1) < buff_size)
            {
                chr_msg_buffer_get(source, (i + 1), &data, &len);
                if (len == PACKET_SINGLE_VAR_SIZE && _check_header(data, EMU_H_VAR_START))
                {
                    ESP_LOGI(TAG, "Found sizes of variables at index %d", i+1);
                    start_index = i + 1;
                    memcpy(mem->single_cnt, &data[HEADER_SIZE], (PACKET_SINGLE_VAR_SIZE - HEADER_SIZE));
                    for (uint8_t i = 0; i < PACKET_SINGLE_VAR_SIZE-2; ++i)
                    {
                        ESP_LOGI(TAG, "type %d: count %d", i, mem->single_cnt[i]);
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
    emu_err_t err = emu_variables_single_create(mem);
    err = emu_variables_arrays_create(source, mem, start_index);
    return err;
}

/*parse switch case generato for handling different types of arrays that needs to be filled*/
#define HANDLE_ARRAY_CASE(ID, TYPE, MEMBER)                                      \
    case ID: {                                                                   \
        arr_##MEMBER##_t *arr = &mem->arr_##MEMBER[arr_index];                   \
        uint16_t total_size = arr->dims[0];                                        \
        for (uint8_t d = 1; d < arr->num_dims; d++) total_size *= arr->dims[d];  \
        total_size *= sizeof(TYPE);                                              \
        if ((offset + bytes_to_copy) <= total_size) {                            \
            memcpy(&arr->data[offset], &data[5], bytes_to_copy);                 \
        } else {                                                                 \
            ESP_LOGW("EMU_PARSE", "Write out of bounds (" #TYPE " table %d)", arr_index); \
        }                                                                        \
        break;                                                                   \
    }

/*parse switch case generatoer for handling different types of single variables
 that needs to be filled*/
#define HANDLE_SINGLE_CASE(ID, TYPE, MEMBER, DATA_LEN)                      \
    case ID: {                                                              \
        uint16_t offset = 2;                                                  \
        uint16_t end = DATA_LEN;                                              \
        while (offset + sizeof(TYPE) < end) {                               \
            uint8_t var_idx = data[offset];                                 \
            memcpy(&mem->MEMBER[var_idx], &data[offset + 1], sizeof(TYPE)); \
            offset += (1 + sizeof(TYPE));                                   \
        }                                                                   \
        break;                                                              \
    }


#define GET_OFFSET(data)   ((uint16_t)(((data[3]) << 8) | (data[4])))
emu_err_t emu_parse_variables_into(chr_msg_buffer_t *source, emu_mem_t *mem) {
    uint8_t *data;
    uint16_t len;
    uint16_t buff_size = chr_msg_buffer_size(source);

    for (uint16_t i = 0; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len <= HEADER_SIZE) continue;

        uint8_t arr_index = data[2];
        uint16_t offset   = GET_OFFSET(data);
        uint16_t bytes_to_copy = len - 5;

        /*switch header type*/
        switch ((emu_header_t)((data[0] << 8) | data[1])) {
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_0, uint8_t,  ui8)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_1, uint16_t, ui16)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_2, uint32_t, ui32)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_3, int8_t,   i8)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_4, int16_t,  i16)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_5, int32_t,  i32)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_6, float,    f)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_7, double,   d)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_8, bool,     b)

            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S0, uint8_t,  u8,  buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S1, uint16_t, u16, buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S2, uint32_t, u32, buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S3, int8_t,   i8,  buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S4, int16_t,  i16, buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S5, int32_t,  i32, buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S6, float,    f,   buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S7, double,   d,   buff_size)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S8, bool,     b,   buff_size)
            default:
                break;
        }
    }
    return EMU_OK;
}

static block_handle_t *_block_create_struct(uint8_t in_cnt, uint8_t q_cnt){
    block_handle_t *block = calloc(1, sizeof(block_handle_t));
    block -> in_cnt = in_cnt;
    block -> q_cnt  = q_cnt;
    block -> in_data_offsets     = calloc(in_cnt, sizeof(uint16_t));
    block -> q_data_offsets      = calloc(q_cnt,  sizeof(uint16_t));
    block->in_data_type_table    = calloc(in_cnt, sizeof(data_types_t));
    block->q_data_type_table     = calloc(q_cnt,  sizeof(data_types_t));
    block->q_connections_table   = calloc(q_cnt,  sizeof(q_connection_t));
    return block;
}

extern void **blocks_structs;

#define IN_Q_STEP 3  //block id (2) + in num (1)
#define READ_U16(DATA, IDX) \
    ((uint16_t)((DATA)[(IDX)]) | ((uint16_t)((DATA)[(IDX)+1]) << 8))
emu_err_t emu_parse_block(chr_msg_buffer_t *source)
{
    static const char* TAG  = "Block allocation";
    uint8_t *data;
    uint16_t len;
    uint16_t buff_size = chr_msg_buffer_size(source);
    uint16_t search_idx = 0;

    while (search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[2] == 0x00)
        {
            ESP_LOGI(TAG, "Dected start header of block type: %d", data[1]);

            uint16_t block_id = READ_U16(data, 3);
            // --- PARSE INPUT COUNT ---
            uint16_t idx = 5;  // points at in_cnt
            uint8_t in_cnt = data[idx];

            // Move idx past: in_cnt (1) + input type table (in_cnt)
            idx++; // to first input type
            uint16_t in_types_start = idx;
            idx += in_cnt;   // skip input types

            // --- PARSE Q COUNT ---
            uint8_t q_cnt = data[idx];
            idx++; // move to first Q type

            block_handle_t *block_ptr = _block_create_struct(in_cnt, q_cnt);
            blocks_structs[block_id] = (void*)block_ptr;
            block_ptr->block_id = block_id;

            // PARSE INPUT TYPES

            uint16_t total_in_size = 0;

            for (uint8_t i = 0; i < in_cnt; i++)
            {
                block_ptr->in_data_offsets[i] = total_in_size;
                data_types_t type = data[in_types_start + i]; 
                block_ptr->in_data_type_table[i] = type;
                total_in_size += data_size(type);
            }

            block_ptr->in_data = calloc(total_in_size, sizeof(uint8_t));

            // PARSE Q TYPES

            uint16_t q_types_start = idx;

            uint16_t total_q_size = 0;
            for (uint8_t i = 0; i < q_cnt; i++)
            {
                block_ptr->q_data_offsets[i] = total_q_size;
                data_types_t type = data[q_types_start + i];
                block_ptr->q_data_type_table[i] = type;
                total_q_size += data_size(type);
            }

            idx += q_cnt;    // move idx to q_used
            block_ptr->q_data = calloc(total_q_size, sizeof(uint8_t));

            ESP_LOGI(TAG, "block block_id:%d  has: %d inputs and %d outputs total size of inputs: %d, size of outputs: %d",
                     block_id, in_cnt, q_cnt, total_in_size, total_q_size);

            // PARSE Q -> CONNECTIONS

            uint8_t q_used = data[idx];
            idx++;
            uint8_t total_lines = 0;
            for (uint8_t q = 0; q < q_used; q++)
            {
                uint8_t conn_cnt = data[idx];
    
                block_ptr->q_connections_table[q].conn_cnt = conn_cnt;
                block_ptr->q_connections_table[q].target_blocks_id_list =
                    (uint16_t *)calloc(conn_cnt, sizeof(uint16_t));

                block_ptr->q_connections_table[q].target_inputs_list =
                    (uint8_t *)calloc(conn_cnt, sizeof(uint8_t));
                idx++; 
                for (uint8_t conn = 0; conn < conn_cnt; conn++)
                {
                    total_lines ++; 
                    block_ptr->q_connections_table[q].target_blocks_id_list[conn] = READ_U16(data, idx);
                    block_ptr->q_connections_table[q].target_inputs_list[conn] = data[idx + 2];

                    ESP_LOGI(TAG, "Q number:%d |line %d| target block block_id:%d, target block in:%d",
                             q, total_lines,
                             block_ptr->q_connections_table[q].target_blocks_id_list[conn],
                             block_ptr->q_connections_table[q].target_inputs_list[conn]);
                    idx += IN_Q_STEP;// move to next line 
                }
            }
            if(total_lines == 0){
                ESP_LOGI(TAG, "No connections detected, block: %d is terminator block", block_ptr->block_id);
            }
            _parse_assign_fuction(data[1],block_id);
            ESP_LOGI(TAG, "Allocated handle for block block_id %d", block_id);

        }
        search_idx++;

    }
    return EMU_OK;
}

extern emu_block_func *blocks_fun_table;
static void _parse_assign_fuction(uint8_t block_type, uint16_t block_id){
    switch (block_type)
    {
    case BLOCK_COMPUTE:
        blocks_fun_table[block_id] = block_compute;
        break;
    case BLOCK_INJECT:
    default:
        break;
    }
}

