#include "emulator_parse.h"
#include "emulator_blocks.h"
#include "emulator_types.h"
#include "utils_block_in_q_access.h"
#include "utils_global_access.h"
#include "block_math.h"

static const char *TAG = "EMU_PARSE";
#define PACKET_SINGLE_VAR_SIZE 11  //2 + 9 * type cnt
#define HEADER_SIZE 2
static void _parse_assign_fuction(uint8_t block_type, uint16_t block_id);

/*********************ARR cheks*****************/
#define STEP_ARR_1D 2 // type, dim1
#define STEP_ARR_2D 3 // type, dim1, dim2
#define STEP_ARR_3D 4 // type, dim1, dim2, dim3
#define GET_HEADER(data)   ((uint16_t)(((data[0]) << 8) | (data[1])))
bool parse_check_arr_header(uint8_t *data, uint8_t *step)
{
    uint16_t header = GET_HEADER(data);
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
bool parse_check_header(uint8_t *data, emu_header_t header)
{   
    return GET_HEADER(data) == header;
}
bool parse_check_arr_packet_size(uint16_t len, uint8_t step)
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
    size_t len;
    size_t start_index = -1;
    size_t buff_size = chr_msg_buffer_size(source);

    // parse single variables
    for (uint16_t i = 0; i < buff_size; ++i)
    {
        chr_msg_buffer_get(source, i, &data, &len);
        /*single variables sizes detact and parse*/
        if (parse_check_header(data, EMU_H_VAR_START) && len == PACKET_SINGLE_VAR_SIZE)
        {
            ESP_LOGI(TAG, "Found count of scalar variables at idx: %d", i);
            start_index = i;
            memcpy(mem->single_cnt, &data[HEADER_SIZE], TYPES_COUNT);
            for (uint8_t j = 0; j < TYPES_COUNT; ++j){
                ESP_LOGI(TAG, "type %s: count %d", DATA_TYPE_NAMES[j], mem->single_cnt[j]);
            }
            break;  
        }
    }
    if (start_index == -1)
    {
        ESP_LOGE(TAG, "Scalar variables sizes not found can't create memory");
        return EMU_ERR_INVALID_DATA;
    }
    //create space for single variables and create pointers
    EMU_RETURN_ON_ERROR(emu_variables_single_create(mem));
    //parse and create arrays
    EMU_RETURN_ON_ERROR(emu_variables_arrays_create(source, mem, start_index));
    return EMU_OK;
}

#define ARR_DATA_START_IDX 5
/*parse switch case generator for handling different types of arrays that needs to be filled*/
#define HANDLE_ARRAY_CASE(HEADER, TYPE, MEMBER, DATA_LEN)                                      \
    case HEADER: {                                                                   \
        arr_##MEMBER##_t *arr = &mem->arr_##MEMBER[arr_index];                   \
        uint16_t total_size = arr->dims[0];                                        \
        for (uint8_t d = 1; d < arr->num_dims; d++) total_size *= arr->dims[d];  \
        total_size *= sizeof(TYPE);                                              \
        if ((offset +(DATA_LEN)) <= total_size) {                            \
            memcpy(&arr->data[offset], &data[ARR_DATA_START_IDX], (DATA_LEN));                 \
        } else {                                                                 \
            ESP_LOGW("Parse into variables", "Write out of bounds (" #TYPE " table %d)", arr_index); \
        }                                                                        \
        break;                                                                   \
    }

/*parse switch case generatoer for handling different types of single variables
 that needs to be filled*/
#define HANDLE_SINGLE_CASE(HEADER, TYPE, MEMBER, DATA_LEN)                      \
    case HEADER: {                                                              \
        uint16_t offset = HEADER_SIZE;                                                  \
        uint16_t end = DATA_LEN;                                              \
        while (offset + sizeof(TYPE) < end) {                               \
            uint8_t var_idx = data[offset];                                 \
            memcpy(&mem->MEMBER[var_idx], &data[offset + 1], sizeof(TYPE)); \
            offset += (1 + sizeof(TYPE));                                   \
        }                                                                   \
        break;                                                              \
    }

#define GET_ARR_START_OFFSET(data)   ((uint16_t)(((data[3]) << 8) | (data[4])))
emu_err_t emu_parse_variables_into(chr_msg_buffer_t *source, emu_mem_t *mem) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (uint16_t i = 0; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len < ARR_DATA_START_IDX) continue;

        uint8_t arr_index = data[HEADER_SIZE];
        uint16_t offset   = GET_ARR_START_OFFSET(data);    
        uint16_t arr_bytes_to_copy = len - ARR_DATA_START_IDX;

        /*switch header type*/
        switch ((emu_header_t)(GET_HEADER(data))) {
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_0, uint8_t,  ui8,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_1, uint16_t, ui16, arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_2, uint32_t, ui32, arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_3, int8_t,   i8,   arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_4, int16_t,  i16,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_5, int32_t,  i32,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_6, float,    f,    arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_7, double,   d,    arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_DATA_8, bool,     b,    arr_bytes_to_copy)

            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S0, uint8_t,  u8,  len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S1, uint16_t, u16, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S2, uint32_t, u32, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S3, int8_t,   i8,  len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S4, int16_t,  i16, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S5, int32_t,  i32, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S6, float,    f,   len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S7, double,   d,   len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_DATA_S8, bool,     b,   len)
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

extern void **emu_global_blocks_structs;

#define IN_Q_STEP 3  //block id (2) + in num (1)
#define READ_U16(DATA, IDX) \
    ((uint16_t)((DATA)[(IDX)]) | ((uint16_t)((DATA)[(IDX)+1]) << 8))
emu_err_t emu_parse_block(chr_msg_buffer_t *source)
{
    static const char* TAG  = "Block allocation";
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint8_t search_idx = 0;
    uint16_t block_id = 0;

    while (search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[2] == 0x00)
        {
            ESP_LOGI(TAG, "Dected start header of block type: %d", data[1]);

            block_id = READ_U16(data, 3);
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
            emu_global_blocks_structs[block_id] = (void*)block_ptr;
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

                    ESP_LOGI(TAG, "Q number:%d |line %d| target block_id:%d, target in:%d",
                             q, total_lines,
                             block_ptr->q_connections_table[q].target_blocks_id_list[conn],
                             block_ptr->q_connections_table[q].target_inputs_list[conn]);
                    idx += IN_Q_STEP;// move to next line 
                }
            }
            if(total_lines == 0){
                ESP_LOGI(TAG, "No connections form block_id: %d", block_ptr->block_id);
            }
            _parse_assign_fuction(data[1],block_id);
            ESP_LOGI(TAG, "Allocated handle for block block_id %d", block_id);


        }
        if ((data[0] == 0xBB) && (data[2] >= 0xF0)) {
            ESP_LOGI(TAG, "Detected global variable reference at block: %d", block_id);
            
            _global_val_acces_t *store = NULL; // Initialize to NULL
            
            // 1. Pass the ADDRESS of store (&store)
            emu_err_t err = _parse_block_mem_acces_data(source, &search_idx, &store);
            
            // 2. Only assign if parsing succeeded AND store is not NULL
            if (err == EMU_OK && store != NULL) {
                block_handle_t** blocks = (block_handle_t**)emu_global_blocks_structs;
                blocks[block_id]->extras = (void*)store;
            } else {
                ESP_LOGE(TAG, "Parsing failed or returned NULL. Error: %d", err);
            }
        }
        search_idx++;

    }
    return EMU_OK;
}
#define ACCES_STEP 5
#define MAX_ARR_DIMS 3

static const char* GLOBAL_ACCESS = "EMU_PARSE";

static _global_val_acces_t* _link_recursive(uint8_t *data, uint16_t *cursor, uint16_t len, 
                                             _global_val_acces_t *pool, uint16_t *pool_idx) 
{
    if (*cursor >= len) return NULL;

    // LOG: Trace node creation
    // ESP_LOGD(GLOBAL_ACCESS, "Linking node at pool_idx=%d, cursor=%d", *pool_idx, *cursor);

    // 1. Consume a node from the pool
    _global_val_acces_t *node = &pool[*pool_idx];
    (*pool_idx)++;

    // 2. Parse the 5 bytes of data for this node
    node->target_type = data[*cursor];
    node->target_idx  = data[*cursor + 1];
    node->target_custom_indices[0] = data[*cursor + 2];
    node->target_custom_indices[1] = data[*cursor + 3];
    node->target_custom_indices[2] = data[*cursor + 4];

    // LOG: Trace node data
    // ESP_LOGI(GLOBAL_ACCESS, "Node parsed: Type=%d Idx=%d Static=[%d,%d,%d]", 
    //          node->target_type, node->target_idx, 
    //          node->target_custom_indices[0], node->target_custom_indices[1], node->target_custom_indices[2]);

    // Advance cursor past the data block
    *cursor += ACCES_STEP;

    // 3. Process the 3 potential children
    // --- Child 0 ---
    if (*cursor < len && data[*cursor] == 0xFF) {
        node->next0 = NULL;
        (*cursor)++; 
    } else {
        node->next0 = _link_recursive(data, cursor, len, pool, pool_idx);
    }

    // --- Child 1 ---
    if (*cursor < len && data[*cursor] == 0xFF) {
        node->next1 = NULL;
        (*cursor)++;
    } else {
        node->next1 = _link_recursive(data, cursor, len, pool, pool_idx);
    }

    // --- Child 2 ---
    if (*cursor < len && data[*cursor] == 0xFF) {
        node->next2 = NULL;
        (*cursor)++;
    } else {
        node->next2 = _link_recursive(data, cursor, len, pool, pool_idx);
    }

    return node;
}

emu_err_t _parse_block_mem_acces_data(chr_msg_buffer_t *source, uint8_t *start, _global_val_acces_t **store) {
    uint8_t *data;
    size_t len;
    
    // Get pointer to raw data inside the buffer
    // NOTE: Check if chr_msg_buffer_get expects a pointer (*start) or value (*start) for the index.
    chr_msg_buffer_get(source, *start, &data, &len);

    // LOG: Print the header to verify what we actually received
    ESP_LOGI(GLOBAL_ACCESS, "Parse Start. Len=%d. Header Bytes: [0]=%02X [1]=%02X [2]=%02X [3]=%02X", 
             len, data[0], data[1], data[2], data[3]);

    if (data[2] == 0xF0) {
        ESP_LOGI(GLOBAL_ACCESS, "Block ID match (0xF0). Starting parse...");

        // --- PASS 1: Count required nodes ---
        uint16_t total_struct_list = 1; 
        uint16_t check_idx = 10; 

        while (check_idx < len) {
            if (data[check_idx] == 0xFF) {
                check_idx++;
            } else {
                total_struct_list++;
                check_idx += ACCES_STEP;
            }
        }

        ESP_LOGI(GLOBAL_ACCESS, "Pass 1 Complete. Total nodes required: %d", total_struct_list);

        // --- PASS 2: Allocate Memory ---
        _global_val_acces_t *pool = calloc(total_struct_list, sizeof(_global_val_acces_t));
        if (!pool) {
            ESP_LOGE(GLOBAL_ACCESS, "Memory allocation failed for %d nodes", total_struct_list);
            return EMU_ERR_MEM_ALLOC; 
        }

        // --- PASS 3: Recursive Linking ---
        uint16_t cursor = 5;      
        uint16_t pool_usage = 0;  

        _global_val_acces_t *root = _link_recursive(data, &cursor, len, pool, &pool_usage);

        // Sanity check
        if (pool_usage != total_struct_list) {
            ESP_LOGW(GLOBAL_ACCESS, "Pool usage mismatch: calc %d vs used %d", total_struct_list, pool_usage);
        } else {
            ESP_LOGI(GLOBAL_ACCESS, "Parse success. Root created.");
        }
        *store = root;
    } else {
        // LOG: Explain why parsing was skipped
        ESP_LOGW(GLOBAL_ACCESS, "Skipped parsing: Expected 0xF0 at data[2], but found %02X", data[2]);
    }
    
    return EMU_OK;
}

extern emu_block_func *emu_global_blocks_functions;
static void _parse_assign_fuction(uint8_t block_type, uint16_t block_id){
    static const char* TAG = "Assign block function";
    switch (block_type)
    {
    case BLOCK_COMPUTE:
        emu_global_blocks_functions[block_id] = block_math;
        ESP_LOGI(TAG, "Assigned compute function to block id %d", block_id);
        break;
    case BLOCK_INJECT:
        ESP_LOGI(TAG, "Block inject detected (No function assigned yet)");
        break;
    default:
        ESP_LOGW(TAG, "Unknown block type %d for block id %d", block_type, block_id);
        break;
    }
}
