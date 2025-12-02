#include "emulator_variables.h"
#include "emulator_parse.h"
#include "emulator.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdarg.h>

static const char *TAG = "DATAHOLDER";
#define HEADER_SIZE 2

/*This macro setup pointers for tables of single varaibles different types*/
#define SETUP_FIELD(base, field, index, ptr, is_array) \
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
#define HANDLE_DATA_TYPE(mem, ENUM, FIELD, CTYPE, TAGSTR)                        \
    case ENUM: {                                                                 \
        typeof((mem)->FIELD[0]) *arr = &(mem)->FIELD[arr_index[ENUM]];           \
        arr->num_dims = step - 1;                                                \
        memcpy(arr->dims, &data[j + 1], step - 1);                               \
        arr->data = (CTYPE *)((mem)->_base_arr_ptr + offset);                    \
        ESP_LOGI(TAG,                                                            \
                 "created at: %d " TAGSTR " table dims: %d, size_x: %d, size_y: %d, size_z: %d", \
                 arr_index[ENUM],                                                \
                 arr->num_dims, arr->dims[0], arr->dims[1], arr->dims[2]);       \
        arr_index[ENUM]++;                                                       \
        break;                                                                   \
    }

/*this macro sum size of total data needed*/
#define ADD_SIZE(total, count, type) ((total) += (count) * sizeof(type))


emu_err_t emu_variables_single_create(emu_mem_t *mem)
{
    if (!mem) {
        return EMU_ERR_INVALID_ARG;
    }
    size_t total_size = 0;
    ADD_SIZE(total_size, mem->single_cnt[0],  int8_t);
    ADD_SIZE(total_size, mem->single_cnt[1],  int16_t);
    ADD_SIZE(total_size, mem->single_cnt[2],  int32_t);
    ADD_SIZE(total_size, mem->single_cnt[3],  uint8_t);
    ADD_SIZE(total_size, mem->single_cnt[4],  uint16_t);
    ADD_SIZE(total_size, mem->single_cnt[5],  uint32_t);
    ADD_SIZE(total_size, mem->single_cnt[6],  float);
    ADD_SIZE(total_size, mem->single_cnt[7],  double);
    ADD_SIZE(total_size, mem->single_cnt[8],  bool);

    mem->_base_ptr = calloc(1, total_size);
    if (!mem->_base_ptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for dataholder (%d bytes)", total_size);
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }
    uint8_t* current_ptr = (uint8_t*)mem->_base_ptr;
    SETUP_FIELD(mem, i8,  0, current_ptr, false);
    SETUP_FIELD(mem, i16, 1, current_ptr, false);
    SETUP_FIELD(mem, i32, 2, current_ptr, false);
    SETUP_FIELD(mem, u8,  3, current_ptr, false);
    SETUP_FIELD(mem, u16, 4, current_ptr, false);
    SETUP_FIELD(mem, u32, 5, current_ptr, false);
    SETUP_FIELD(mem, f,   6, current_ptr, false);
    SETUP_FIELD(mem, d,   7, current_ptr, false);
    SETUP_FIELD(mem, b,   8, current_ptr, false);

    ESP_LOGI(TAG, "Single variables dataholder created successfully");
    return EMU_OK;
}


emu_err_t emu_variables_arrays_create(chr_msg_buffer_t *source, emu_mem_t *mem, uint16_t start_index)
{
    uint8_t *data;
    uint16_t len;
    uint64_t buff_size = chr_msg_buffer_size(source);
    uint8_t step;
    uint64_t total_size = 0;
    start_index += 1;

    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (_check_arr_header(data, &step) && _check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                mem->arr_cnt[(data_types_t)data[j]]++;
                uint64_t table_cnt = data[j + 1];
                if (step >= 3) {table_cnt *= data[j + 2];}
                if (step == 4) {table_cnt *= data[j + 3];}
                total_size += data_size((data_types_t)data[j]) * table_cnt;
            }
        }
    }
    ESP_LOGI(TAG, "Total array size of arrays: %lldB", total_size);
    for (uint8_t i = 0 ; i < 9 ; i++)
    {ESP_LOGI(TAG,"need %d slots for %d type table", mem->arr_cnt[i], i);}
    size_t handle_size = 0;
    ADD_SIZE(handle_size, mem->arr_cnt[0], arr_ui8_t);
    ADD_SIZE(handle_size, mem->arr_cnt[1], arr_ui16_t);
    ADD_SIZE(handle_size, mem->arr_cnt[2], arr_ui32_t);
    ADD_SIZE(handle_size, mem->arr_cnt[3], arr_i8_t);
    ADD_SIZE(handle_size, mem->arr_cnt[4], arr_i16_t);
    ADD_SIZE(handle_size, mem->arr_cnt[5], arr_i32_t);
    ADD_SIZE(handle_size, mem->arr_cnt[6], arr_f_t);
    ADD_SIZE(handle_size, mem->arr_cnt[7], arr_d_t);
    ADD_SIZE(handle_size, mem->arr_cnt[8], arr_b_t);

    mem->_base_arr_handle_ptr = calloc(1, handle_size);
    if (!mem->_base_arr_handle_ptr){
        ESP_LOGW(TAG, "Arrays handle allocation failed");
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }

    for (uint8_t i = 0 ; i < 9 ; i++) 
    {ESP_LOGI(TAG,"allocated %d slots for %d type table", mem->arr_cnt[i], i);}

    uint8_t *current_ptr = (uint8_t*)mem->_base_arr_handle_ptr;
    SETUP_FIELD(mem, arr_ui8,  0, current_ptr, true);
    SETUP_FIELD(mem, arr_ui16, 1, current_ptr, true);
    SETUP_FIELD(mem, arr_ui32, 2, current_ptr, true);
    SETUP_FIELD(mem, arr_i8,   3, current_ptr, true);
    SETUP_FIELD(mem, arr_i16,  4, current_ptr, true);
    SETUP_FIELD(mem, arr_i32,  5, current_ptr, true);
    SETUP_FIELD(mem, arr_f,    6, current_ptr, true);
    SETUP_FIELD(mem, arr_d,    7, current_ptr, true);
    SETUP_FIELD(mem, arr_b,    8, current_ptr, true);

    mem->_base_arr_ptr = calloc(1, total_size);
    if (!mem->_base_arr_ptr){
        ESP_LOGW(TAG, "Arrays allocation failed");
        emu_variables_reset(mem);
        return EMU_ERR_NO_MEMORY;
    }
    uint8_t arr_index[9] = {0};
    size_t offset = 0;

    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (_check_arr_header(data, &step) && _check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                data_types_t type = (data_types_t)data[j];
                size_t elem_count = data[j + 1];
                if (step >= 3) elem_count *= data[j + 2];
                if (step == 4) elem_count *= data[j + 3];
                size_t bytes_needed = elem_count * data_size(type);

                switch (type) {
                    HANDLE_DATA_TYPE(mem, DATA_UI8,  arr_ui8,  uint8_t,  "uint8_t");
                    HANDLE_DATA_TYPE(mem, DATA_UI16, arr_ui16, uint16_t, "uint16_t");
                    HANDLE_DATA_TYPE(mem, DATA_UI32, arr_ui32, uint32_t, "uint32_t");
                    HANDLE_DATA_TYPE(mem, DATA_I8,   arr_i8,   int8_t,   "int8_t");
                    HANDLE_DATA_TYPE(mem, DATA_I16,  arr_i16,  int16_t,  "int16_t");
                    HANDLE_DATA_TYPE(mem, DATA_I32,  arr_i32,  int32_t,  "int32_t");
                    HANDLE_DATA_TYPE(mem, DATA_F,    arr_f,    float,    "float");
                    HANDLE_DATA_TYPE(mem, DATA_D,    arr_d,    double,   "double");
                    HANDLE_DATA_TYPE(mem, DATA_B,    arr_b,    bool,     "bool");
                    default:
                        ESP_LOGE(TAG, "Unknown array data type");
                        break;
                }
                offset += bytes_needed;
            }
        }
    }
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
                       uint8_t idx_table[3], double *out_value)
{
    if (!out_value) return 0x100;

    size_t flat_idx = SIZE_MAX;
    bool is_scalar =
        (idx_table[0] == UINT8_MAX &&
         idx_table[1] == UINT8_MAX &&
         idx_table[2] == UINT8_MAX);

    /* Compute flat index if array */
    if (!is_scalar) {

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
            default: return 0x100;
        }

        if (flat_idx == (size_t)-1)
            return 0x100;
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

        default: return 0x100;
    }

    return EMU_OK;
}

static const char *TAG_MEM_GET_RECURSIVE = "mem_get_global_recursive";
emu_err_t mem_get_global_recursive(_global_val_acces_t *b, double *value_out)
{
    if (!value_out || !b) {
        ESP_LOGW(TAG_MEM_GET_RECURSIVE, "NULL mem_get_global_recursive input struct or no output provided");
        return EMU_ERR_NULL_POINTER;}

    uint8_t idx_table[MAX_ARR_DIMS];
    _global_val_acces_t *next_arr[MAX_ARR_DIMS] = {b->next0, b->next1, b->next2};

    for (int i = 0; i < MAX_ARR_DIMS; i++) {

        if (next_arr[i]) {
            double tmp;
            emu_err_t e = mem_get_global_recursive(next_arr[i], &tmp);
            if (e != EMU_OK) return e;

            if (tmp < 0.0) {idx_table[i] = 0;}
            else if (tmp > 254.0) {idx_table[i] = 254;}
            else {idx_table[i] = (uint8_t)round(tmp);}

            //ESP_LOGI(TAG_MEM_GET_RECURSIVE, "Resolved next%d => raw=%f => idx=%d", i, tmp, idx_table[i]);

        } else {
            idx_table[i] = b->target_custom_indices[i];
            ESP_LOGI(TAG_MEM_GET_RECURSIVE, "Using static index%d: %d", i, idx_table[i]);
        }
    }

   //check if scalar or array access
    bool is_scalar = (idx_table[0] == UINT8_MAX && idx_table[1] == UINT8_MAX &&idx_table[2] == UINT8_MAX);

    ESP_LOGI(TAG, "Target type=%d idx=%u scalar=%d idx=[%d,%d,%d]",
             b->target_type, (unsigned)b->target_idx, is_scalar,
             idx_table[0], idx_table[1], idx_table[2]);

    /* Bounds checking */
    if (is_scalar) {
        if (b->target_idx >= mem.single_cnt[b->target_type]) {
            ESP_LOGE(TAG_MEM_GET_RECURSIVE, "Scalar index out of range");
            return EMU_ERR_MEM_INVALID_INDEX;
        }
    } else {
        if (b->target_idx >= mem.arr_cnt[b->target_type]) {
            ESP_LOGE(TAG_MEM_GET_RECURSIVE, "Array index out of range");
            return EMU_ERR_MEM_INVALID_INDEX;
        }
    }

    double val;
    emu_err_t err = mem_get_as_d(b->target_type, b->target_idx, idx_table, &val);
    if (err != EMU_OK) {
        return err;
    }

    //ESP_LOGI(TAG_MEM_GET_RECURSIVE,"Accessing type=%d idx=%u indices=[%d,%d,%d] => value=%f", b->target_type, (uint8_t)b->target_idx, idx_table[0], idx_table[1], idx_table[2], val);

    *value_out = val;
    return EMU_OK;
}

