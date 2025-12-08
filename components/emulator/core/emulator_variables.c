#include "emulator_variables.h"
#include "emulator_errors.h"
#include "emulator_parse.h"
#include "emulator_interface.h"
#include "emulator_types.h"
#include "esp_log.h"


const char *DATA_TYPE_TO_STR[9] = {
    "DATA_UI8",
    "DATA_UI16",
    "DATA_UI32",
    "DATA_I8",
    "DATA_I16",
    "DATA_I32",
    "DATA_F",
    "DATA_D",
    "DATA_B"
};

static const char *TAG = "EMU_VARIABLES";
#define HEADER_SIZE 2

/*This macro setup pointers for tables of single varaibles different types*/
#define SETUP_PTR(base, field, index, ptr, is_array) \
({\
size_t cnt = is_array ? base->arr_cnt[index] : base->single_cnt[index];\
(base)->field = (typeof((base)->field))(ptr);\
(ptr) += cnt * sizeof(*(base)->field);\
(base)->field;\
})\

/*This macro set up pointers for single variables*/
#define SETUP_SINGLE_VAR_PTR(mem, field, idx) ({ \
        typeof((mem)->field) _p = (typeof(_p))current_ptr; \
        (mem)->field = _p; \
        current_ptr += sizes[idx] * sizeof(*_p); \
})

/*This macro set up pointers for arrays*/
#define HANDLE_ARR_DATA_TYPE(mem, ENUM, FIELD, CTYPE)                        \
    case ENUM: {                                                                 \
        typeof((mem)->FIELD[0]) *arr = &(mem)->FIELD[arr_index[ENUM]];           \
        arr->num_dims = step - 1;                                                \
        memcpy(arr->dims, &data[j + 1], step - 1);                               \
        arr->data = (CTYPE *)((mem)->_base_arr_ptr + offset);                    \
        ESP_LOGI(TAG,                                                            \
                 "created at: %d type: %s table dims: %d, size_x: %d, size_y: %d, size_z: %d", \
                 arr_index[ENUM], DATA_TYPE_TO_STR[ENUM],                                               \
                 arr->num_dims, arr->dims[0], arr->dims[1], arr->dims[2]);       \
        arr_index[ENUM]++;                                                       \
        break;                                                                   \
    }


/*this macro sum size of total bytes needed*/
#define ADD_TO_TOTAL_SIZE(total, count, type) ((total) += (count) * sizeof(type))

/**
 * @brief This function allocate space for all single variables 
 */
emu_err_t emu_variables_single_create(emu_mem_t *mem)
{
    if (!mem) {
        LOG_E(TAG, "No mem struct provided");
        return EMU_ERR_NULL_POINTER;
    }
    size_t total_size = 0;
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[0],  int8_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[1],  int16_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[2],  int32_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[3],  uint8_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[4],  uint16_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[5],  uint32_t);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[6],  float);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[7],  double);
    ADD_TO_TOTAL_SIZE(total_size, mem->single_cnt[8],  bool);

    mem->_base_ptr = calloc(1, total_size);
    if (!mem->_base_ptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for dataholder (%d bytes)", total_size);
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }
    uint8_t* current_ptr = (uint8_t*)mem->_base_ptr;
    SETUP_PTR(mem, i8,  0, current_ptr, false);
    SETUP_PTR(mem, i16, 1, current_ptr, false);
    SETUP_PTR(mem, i32, 2, current_ptr, false);
    SETUP_PTR(mem, u8,  3, current_ptr, false);
    SETUP_PTR(mem, u16, 4, current_ptr, false);
    SETUP_PTR(mem, u32, 5, current_ptr, false);
    SETUP_PTR(mem, f,   6, current_ptr, false);
    SETUP_PTR(mem, d,   7, current_ptr, false);
    SETUP_PTR(mem, b,   8, current_ptr, false);

    ESP_LOGI(TAG, "Single variables dataholder created successfully");
    return EMU_OK;
}

/**
*@brief this function parse source buffer and allocate array space
*/
emu_err_t emu_variables_arrays_create(chr_msg_buffer_t *source, emu_mem_t *mem, size_t start_index)
{
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint8_t step;
    size_t total_size = 0;
    start_index += 1;
    LOG_I(TAG, "Now calculating size needed");
    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (parse_check_arr_header(data, &step) && parse_check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                mem->arr_cnt[(data_types_t)data[j]]++;
                size_t table_cnt = data[j + 1];
                if (step >= 3) {table_cnt *= data[j + 2];}
                if (step == 4) {table_cnt *= data[j + 3];}
                total_size += data_size((data_types_t)data[j]) * table_cnt;
            }
        }
    }
    LOG_I(TAG, "Total array size of arrays: %dB", total_size);
    for (uint8_t i = 0 ; i < TYPES_COUNT ; i++)
    {LOG_I(TAG,"need %d slots for type: %s table", mem->arr_cnt[i], DATA_TYPE_TO_STR[i]);}
    size_t handle_size = 0;
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[0], arr_ui8_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[1], arr_ui16_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[2], arr_ui32_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[3], arr_i8_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[4], arr_i16_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[5], arr_i32_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[6], arr_f_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[7], arr_d_t);
    ADD_TO_TOTAL_SIZE(handle_size, mem->arr_cnt[8], arr_b_t);

    mem->_base_arr_handle_ptr = calloc(1, handle_size);
    if (!mem->_base_arr_handle_ptr){
        ESP_LOGE(TAG, "Arrays handle allocation failed");
        LOG_I(TAG, "Resetting after fail");
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }

    for (uint8_t i = 0 ; i < TYPES_COUNT ; i++) 
    {ESP_LOGI(TAG,"allocated %d slots for type : %s table",mem->arr_cnt[i], DATA_TYPE_TO_STR[i]);}

    LOG_I(TAG, "Now setting up poiters to arrays");
    uint8_t *current_ptr = (uint8_t*)mem->_base_arr_handle_ptr;
    SETUP_PTR(mem, arr_ui8,  0, current_ptr, true);
    SETUP_PTR(mem, arr_ui16, 1, current_ptr, true);
    SETUP_PTR(mem, arr_ui32, 2, current_ptr, true);
    SETUP_PTR(mem, arr_i8,   3, current_ptr, true);
    SETUP_PTR(mem, arr_i16,  4, current_ptr, true);
    SETUP_PTR(mem, arr_i32,  5, current_ptr, true);
    SETUP_PTR(mem, arr_f,    6, current_ptr, true);
    SETUP_PTR(mem, arr_d,    7, current_ptr, true);
    SETUP_PTR(mem, arr_b,    8, current_ptr, true);

    mem->_base_arr_ptr = calloc(1, total_size);
    if (!mem->_base_arr_ptr){
        ESP_LOGW(TAG, "Arrays allocation failed");
        LOG_I(TAG, "Resetting after fail");
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }

    uint8_t arr_index[TYPES_COUNT] = {0};
    size_t offset = 0;
    LOG_I(TAG, "Now setting up arrays");
    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (parse_check_arr_header(data, &step) && parse_check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                data_types_t type = (data_types_t)data[j];
                size_t elem_count = data[j + 1];
                if (step >= 3) elem_count *= data[j + 2];
                if (step == 4) elem_count *= data[j + 3];
                size_t bytes_needed = elem_count * data_size(type);

                switch (type) {
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI8,  arr_ui8,  uint8_t  );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI16, arr_ui16, uint16_t );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI32, arr_ui32, uint32_t );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I8,   arr_i8,   int8_t   );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I16,  arr_i16,  int16_t  );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I32,  arr_i32,  int32_t  );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_F,    arr_f,    float    );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_D,    arr_d,    double   );
                    HANDLE_ARR_DATA_TYPE(mem, DATA_B,    arr_b,    bool     );
                    default:
                        ESP_LOGE(TAG, "Unknown array data type");
                        emu_variables_reset(mem);
                        return EMU_ERR_MEM_INVALID_DATATYPE;

                }
                offset += bytes_needed;
            }
        }
    }
    ESP_LOGI(TAG, "Array variables dataholder created successfully");
    return EMU_OK;
}


void emu_variables_reset(emu_mem_t *mem)
{
    if (!mem) return;
    free(mem->_base_ptr);
    free(mem->_base_arr_ptr);
    free(mem->_base_arr_handle_ptr);
    *mem = (emu_mem_t){0};
    ESP_LOGI(TAG, "Dataholder memory freed");
}

emu_err_t mem_get_as_d(data_types_t type, size_t var_idx,
                       uint8_t idx_table[MAX_ARR_DIMS], double *out_value)
{
    if (!out_value){
        LOG_W(TAG, "No output value provided to mem_get_as_d");
        return EMU_ERR_NULL_POINTER;}

    size_t flat_idx = SIZE_MAX;
    bool is_scalar =
        (idx_table[0] == UINT8_MAX &&
         idx_table[1] == UINT8_MAX &&
         idx_table[2] == UINT8_MAX);

    /* Compute flat index if array */
    if (!is_scalar) {
        LOG_I(TAG, "mem_get_as_d: provided type %s, at idx %d is array", DATA_TYPE_TO_STR[type], var_idx);
        switch (type) {
            case DATA_UI8:   flat_idx = _ARR_FLAT_IDX(mem.arr_ui8[var_idx].num_dims, mem.arr_ui8[var_idx].dims, idx_table); break;
            case DATA_UI16:  flat_idx = _ARR_FLAT_IDX(mem.arr_ui16[var_idx].num_dims, mem.arr_ui16[var_idx].dims, idx_table); break;
            case DATA_UI32:  flat_idx = _ARR_FLAT_IDX(mem.arr_ui32[var_idx].num_dims, mem.arr_ui32[var_idx].dims, idx_table); break;
            case DATA_I8:    flat_idx = _ARR_FLAT_IDX(mem.arr_i8[var_idx].num_dims, mem.arr_i8[var_idx].dims, idx_table); break;
            case DATA_I16:   flat_idx = _ARR_FLAT_IDX(mem.arr_i16[var_idx].num_dims, mem.arr_i16[var_idx].dims, idx_table); break;
            case DATA_I32:   flat_idx = _ARR_FLAT_IDX(mem.arr_i32[var_idx].num_dims, mem.arr_i32[var_idx].dims, idx_table); break;
            case DATA_F:     flat_idx = _ARR_FLAT_IDX(mem.arr_f[var_idx].num_dims, mem.arr_f[var_idx].dims, idx_table); break;
            case DATA_D:     flat_idx = _ARR_FLAT_IDX(mem.arr_d[var_idx].num_dims, mem.arr_d[var_idx].dims, idx_table); break;
            case DATA_B:     flat_idx = _ARR_FLAT_IDX(mem.arr_b[var_idx].num_dims, mem.arr_b[var_idx].dims, idx_table); break;
            default: return EMU_ERR_MEM_INVALID_DATATYPE;
        }
        //(macro returns -1 if cannot acces chosen cell)
        if (flat_idx == (size_t)-1){
            LOG_E(TAG, "mem_get_as_d: while accesing with idx %d, %d ,%d, tried to access out of array bonds", idx_table[0], idx_table[1], idx_table[2]);
            return EMU_ERR_MEM_OUT_OF_BOUNDS;
        }
    }else{
        LOG_I(TAG, "mem_get_as_d: provided type %s, at idx %d is scalar", DATA_TYPE_TO_STR[type], var_idx);
    }

    /* Extract value */
    switch (type) {
        case DATA_UI8:  *out_value = is_scalar ? mem.u8[var_idx]  : mem.arr_ui8[var_idx].data[flat_idx]; break;
        case DATA_UI16: *out_value = is_scalar ? mem.u16[var_idx] : mem.arr_ui16[var_idx].data[flat_idx]; break;
        case DATA_UI32: *out_value = is_scalar ? mem.u32[var_idx] : mem.arr_ui32[var_idx].data[flat_idx]; break;

        case DATA_I8:   *out_value = is_scalar ? mem.i8[var_idx]  : mem.arr_i8[var_idx].data[flat_idx]; break;
        case DATA_I16:  *out_value = is_scalar ? mem.i16[var_idx] : mem.arr_i16[var_idx].data[flat_idx]; break;
        case DATA_I32:  *out_value = is_scalar ? mem.i32[var_idx] : mem.arr_i32[var_idx].data[flat_idx]; break;

        case DATA_F:    *out_value = is_scalar ? mem.f[var_idx]   : mem.arr_f[var_idx].data[flat_idx]; break;
        case DATA_D:    *out_value = is_scalar ? mem.d[var_idx]   : mem.arr_d[var_idx].data[flat_idx]; break;

        case DATA_B:    *out_value = is_scalar ? mem.b[var_idx]   : mem.arr_b[var_idx].data[flat_idx]; break;

        default: return
        EMU_ERR_UNLIKELY;
    }
    return EMU_OK;
}

emu_err_t mem_set_safe(data_types_t type, uint8_t idx, uint8_t idx_table[MAX_ARR_DIMS], double value) {
    
    if (!(type == DATA_F || type == DATA_D)) {
        value = round(value);
        LOG_I(TAG, "mem_set_safe: Value after rounding %lf", value);
    }

    switch (type) {
        case DATA_UI8:  { if (value < 0) value = 0; if (value > UINT8_MAX) value = UINT8_MAX; } break;
        case DATA_UI16: { if (value < 0) value = 0; if (value > UINT16_MAX) value = UINT16_MAX; } break;
        case DATA_UI32: { if (value < 0) value = 0; if (value > UINT32_MAX) value = UINT32_MAX; } break;
        case DATA_I8:   { if (value < INT8_MIN) value = INT8_MIN; if (value > INT8_MAX) value = INT8_MAX; } break;
        case DATA_I16:  { if (value < INT16_MIN) value = INT16_MIN; if (value > INT16_MAX) value = INT16_MAX; } break;
        case DATA_I32:  { if (value < INT32_MIN) value = INT32_MIN; if (value > INT32_MAX) value = INT32_MAX; } break;
        case DATA_F:    { value = (double)((float)value); } break;
        case DATA_D:    {} break;
        case DATA_B:    { value = (double)(bool)value; } break;
    }

    // 2. Check for Scalar Access (Special index signature)
    // Assuming 3 dimensions being UINT8_MAX implies a scalar variable access
    if ((idx_table[0] == UINT8_MAX) && (idx_table[1] == UINT8_MAX) && (idx_table[2] == UINT8_MAX)) {
        LOG_I(TAG, "mem_set_safe: setting up scalar");
        switch(type) {
            case DATA_UI8:  mem.u8[idx]  = (uint8_t)(value); break;
            case DATA_UI16: mem.u16[idx] = (uint16_t)(value); break;
            case DATA_UI32: mem.u32[idx] = (uint32_t)(value); break;
            case DATA_I8:   mem.i8[idx]  = (int8_t)(value); break;
            case DATA_I16:  mem.i16[idx] = (int16_t)(value); break;
            case DATA_I32:  mem.i32[idx] = (int32_t)(value); break;
            case DATA_F:    mem.f[idx]   = (float)(value); break;
            case DATA_D:    mem.d[idx]   = (double)(value); break;
            case DATA_B:    mem.b[idx]   = (bool)(value); break;
            default: return EMU_ERR_UNLIKELY; // Unknown type
        }
        return EMU_OK; // Return success for scalar set
    }else {
        size_t flat_idx = (size_t)-1;

        switch(type) {
            case DATA_UI8:
                flat_idx = _ARR_FLAT_IDX(mem.arr_ui8[idx].num_dims, mem.arr_ui8[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_ui8[idx].data[flat_idx] = (uint8_t)(value); }
                break;
            case DATA_UI16:
                flat_idx = _ARR_FLAT_IDX(mem.arr_ui16[idx].num_dims, mem.arr_ui16[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_ui16[idx].data[flat_idx] = (uint16_t)(value); }
                break;
            case DATA_UI32:
                flat_idx = _ARR_FLAT_IDX(mem.arr_ui32[idx].num_dims, mem.arr_ui32[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_ui32[idx].data[flat_idx] = (uint32_t)(value); }
                break;
            case DATA_I8:
                flat_idx = _ARR_FLAT_IDX(mem.arr_i8[idx].num_dims, mem.arr_i8[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_i8[idx].data[flat_idx] = (int8_t)(value); }
                break;
            case DATA_I16:
                flat_idx = _ARR_FLAT_IDX(mem.arr_i16[idx].num_dims, mem.arr_i16[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_i16[idx].data[flat_idx] = (int16_t)(value); }
                break;
            case DATA_I32:
                flat_idx = _ARR_FLAT_IDX(mem.arr_i32[idx].num_dims, mem.arr_i32[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_i32[idx].data[flat_idx] = (int32_t)(value); }
                break;
            case DATA_F:
                flat_idx = _ARR_FLAT_IDX(mem.arr_f[idx].num_dims, mem.arr_f[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_f[idx].data[flat_idx] = (float)(value); }
                break;
            case DATA_D:
                flat_idx = _ARR_FLAT_IDX(mem.arr_d[idx].num_dims, mem.arr_d[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_d[idx].data[flat_idx] = (double)(value); }
                break;
            case DATA_B:
                flat_idx = _ARR_FLAT_IDX(mem.arr_b[idx].num_dims, mem.arr_b[idx].dims, idx_table);
                if(flat_idx != (size_t)-1) { mem.arr_b[idx].data[flat_idx] = (bool)(value); }
                break;
            default:
                return EMU_ERR_UNLIKELY;
        }

        // Return error if index calculation failed
        if (flat_idx == (size_t)-1) {
            LOG_E(TAG, "mem_set_safe: tried to wirte out of array bonds");
            return EMU_ERR_MEM_OUT_OF_BOUNDS;
        }
        return EMU_OK;
    }
}