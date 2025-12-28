#include "emulator_variables.h"
#include "emulator_errors.h"
#include "emulator_interface.h"
#include "emulator_types.h"
#include "utils_parse.h"
#include "esp_log.h"

static const char *TAG = "EMU_VARIABLES";

emu_err_t mem_set_safe(data_types_t type, uint8_t idx, uint8_t idx_table[MAX_ARR_DIMS], double value) {
    emu_err_t err  = EMU_OK;
    static const char* TAG = "MEM SET SAFE";
    if (!(type == DATA_F || type == DATA_D)) {
        value = round(value);
        LOG_I(TAG, "Value after rounding %lf", value);
    }

    /*Clam value to match type sizes*/
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


    /*Scalar set [FF,FF,FF]*/
    if ((idx_table[0] == UINT8_MAX) && (idx_table[1] == UINT8_MAX) && (idx_table[2] == UINT8_MAX)) {
        LOG_I(TAG, "Setting up scalar now for value %lf", value);
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
            default:
                err = EMU_ERR_UNLIKELY;
                return err; // Unknown type
        }
        return err; // Return success for scalar set
    }else {
        size_t flat_idx = (size_t)-1;
        LOG_I(TAG, "Setting up array value for %lf at [%d, %d, %d]", value, idx_table[0], idx_table[1], idx_table[2]);
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
                err = EMU_ERR_UNLIKELY;
                return err;
        }

        // Return error if index calculation failed
        if (flat_idx == (size_t)-1) {
            LOG_E(TAG, "Tried to set variable with invalid index (check if variable exist)");
            err = EMU_ERR_MEM_OUT_OF_BOUNDS;
            return err;
        }
        return err;
    }
}


#define HEADER_SIZE 2
bool _parse_check_arr_header(uint8_t *data, uint8_t *step);

bool _parse_check_arr_packet_size(uint16_t len, uint8_t step)
{
    if (len <= HEADER_SIZE)
    {
        return false;
    }
    return (len - HEADER_SIZE) % step == 0;
}

/*This macro setup pointers for tables of single varaibles different types*/
#define SETUP_PTR(base, field, index, ptr)                \
    ({                                                   \
        size_t cnt = (base)->arr_cnt[index];              \
        (base)->field = (typeof((base)->field))(ptr);     \
        (ptr) += cnt * sizeof(*(base)->field);            \
    })


/*This macro set up pointers for arrays*/
#define HANDLE_ARR_DATA_TYPE(mem, ENUM, FIELD, CTYPE, DATA_PTR)               \
    case ENUM: {                                                             \
        typeof((mem)->FIELD[0]) *arr =                                       \
            &((mem)->FIELD[arr_index[ENUM]]);                                \
                                                                             \
        arr->num_dims = step - 1;                                             \
        memcpy(arr->dims, &data[j + 1], step - 1);                            \
                                                                             \
        arr->data = (CTYPE *)((uint8_t *)(DATA_PTR) +                        \
                              arr_data_offset[ENUM]);                       \
                                                                             \
        ESP_LOGI(TAG,                                                        \
                 "created at: %d type: %s dims: %d [%d,%d,%d]",              \
                 arr_index[ENUM],                                            \
                 DATA_TYPE_TO_STR[ENUM],                                     \
                 arr->num_dims,                                              \
                 arr->dims[0], arr->dims[1], arr->dims[2]);                  \
                                                                             \
        arr_data_offset[ENUM] += bytes_needed;                               \
        arr_index[ENUM]++;                                                   \
        break;                                                               \
    }


/*this macro sum size of total bytes needed*/
#define ADD_TO_TOTAL_SIZE(total, count, type) ((total) += (count) * sizeof(type))

/**
 * @brief This function allocate space for all single variables 
 */
emu_result_t emu_variables_single_create(emu_mem_t *mem)
{   
    emu_result_t res = {.code = EMU_OK};
    if (!mem) {
        LOG_E(TAG, "No mem struct provided to allocate single variables");
        res.code = EMU_ERR_NULL_PTR;
        res.abort = true;
        return res;
    }   
    if(mem->single_cnt[0] > 0 ){
        mem->u8 = calloc(mem->single_cnt[0], data_size(0));
        if(!mem->u8){goto error;}
    }
    if(mem->single_cnt[1] > 0 ){
        mem->u16 = calloc(mem->single_cnt[1], data_size(1));
        if(!mem->u16){goto error;}
    }
    if(mem->single_cnt[2] > 0 ){
        mem->u32 = calloc(mem->single_cnt[2], data_size(2));
        if(!mem->u32){goto error;}
    }   
    if(mem->single_cnt[3] > 0 ){
        mem->i8 = calloc(mem->single_cnt[3], data_size(3));
        if(!mem->i8){goto error;}
    }
    if(mem->single_cnt[4] > 0 ){
        mem->i16 = calloc(mem->single_cnt[4], data_size(4));
        if(!mem->i16){goto error;}
    }
    if(mem->single_cnt[5] > 0 ){
        mem->i32 = calloc(mem->single_cnt[5], data_size(5));
        if(!mem->i32){goto error;}
    }
    if(mem->single_cnt[6] > 0 ){
        mem->f = calloc(mem->single_cnt[6], data_size(6));
        if(!mem->f){goto error;}
    }
    if(mem->single_cnt[7] > 0 ){
        mem->d = calloc(mem->single_cnt[7], data_size(7));
        if(!mem->d){goto error;}
    }
    if(mem->single_cnt[8] > 0 ){
        mem->b = calloc(mem->single_cnt[8], data_size(8));
        if(!mem->b){goto error;}
    }
    LOG_I(TAG, "Single variables dataholder created successfully");
    return res;
    error:
        LOG_E(TAG, "Failed to create memory for single variables");
        emu_variables_reset(mem);
        res.code = EMU_ERR_NO_MEM;
        res.abort = true;
        return res;
}

/**
*@brief this function parse source buffer and allocate array space
*/
emu_result_t emu_variables_arrays_create(chr_msg_buffer_t *source, emu_mem_t *mem, size_t start_index)
{
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint8_t step;
    emu_result_t res = {.code = EMU_OK};
    
    start_index += 1;
    LOG_I(TAG, "Now calculating total arrays count of each type");
    size_t total_sizes_table[TYPES_COUNT] = {0};
    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (_parse_check_arr_header(data, &step) && _parse_check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                mem->arr_cnt[(data_types_t)data[j]]++;
                size_t table_cnt = data[j + 1];
                if (step >= 3) {table_cnt *= data[j + 2];}
                if (step == 4) {table_cnt *= data[j + 3];}
                total_sizes_table[(data_types_t)data[j]] += data_size((data_types_t)data[j]) * table_cnt;
            }
        }
    }
    for (uint8_t i = 0 ; i < TYPES_COUNT ; i++){
        LOG_I(TAG,"need %d slots for type: %s table", mem->arr_cnt[i], DATA_TYPE_TO_STR[i]);
        LOG_I(TAG,"Total arrays size of %s = %dB",DATA_TYPE_TO_STR[i], total_sizes_table[i]);
    }
    size_t arrays_descriptors_size = 0;
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[0], arr_ui8_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[1], arr_ui16_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[2], arr_ui32_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[3], arr_i8_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[4], arr_i16_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[5], arr_i32_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[6], arr_f_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[7], arr_d_t);
    ADD_TO_TOTAL_SIZE(arrays_descriptors_size, mem->arr_cnt[8], arr_b_t);

    if(arrays_descriptors_size > 0){
        mem->_base_arr_handle_ptr = calloc(1, arrays_descriptors_size);
        if (!mem->_base_arr_handle_ptr){
            LOG_E(TAG, "Arrays descriptors (handles) allocation failed");
            goto error;
        }
    }

    for (uint8_t i = 0 ; i < TYPES_COUNT ; i++){
        ESP_LOGI(TAG,"allocated %d slots for type : %s table",mem->arr_cnt[i], DATA_TYPE_TO_STR[i]);
    }

    LOG_I(TAG, "Now setting up poiters to arrays");
    uint8_t *current_ptr = (uint8_t*)mem->_base_arr_handle_ptr;
    SETUP_PTR(mem, arr_ui8,  0, current_ptr);
    SETUP_PTR(mem, arr_ui16, 1, current_ptr);
    SETUP_PTR(mem, arr_ui32, 2, current_ptr);
    SETUP_PTR(mem, arr_i8,   3, current_ptr);
    SETUP_PTR(mem, arr_i16,  4, current_ptr);
    SETUP_PTR(mem, arr_i32,  5, current_ptr);
    SETUP_PTR(mem, arr_f,    6, current_ptr);
    SETUP_PTR(mem, arr_d,    7, current_ptr);
    SETUP_PTR(mem, arr_b,    8, current_ptr);

    if(mem->arr_cnt[0] > 0){
        mem->arr_data_ui8 = calloc(total_sizes_table[0], data_size(0));
        if(!mem->arr_data_ui8){goto error;}
    }
    if(mem->arr_cnt[1] > 0){
        mem->arr_data_ui16 = calloc(total_sizes_table[1], data_size(1));
        if(!mem->arr_data_ui16){goto error;}
    }
    if(mem->arr_cnt[2] > 0){
        mem->arr_data_ui32 = calloc(total_sizes_table[2], data_size(2));
        if(!mem->arr_data_ui32){goto error;}
    }
    if(mem->arr_cnt[3] > 0){
        mem->arr_data_i8 = calloc(total_sizes_table[3], data_size(3));
        if(!mem->arr_data_i8){goto error;}
    }
    if(mem->arr_cnt[4] > 0){
        mem->arr_data_i16 = calloc(total_sizes_table[4], data_size(4));
        if(!mem->arr_data_i16){goto error;}
    }
    if(mem->arr_cnt[5] > 0){
        mem->arr_data_i32 = calloc(total_sizes_table[5], data_size(5));
        if(!mem->arr_data_i32){goto error;}
    }
    if(mem->arr_cnt[6] > 0){
        mem->arr_data_f = calloc(total_sizes_table[6], data_size(6));
        if(!mem->arr_data_f){goto error;}
    }
    if(mem->arr_cnt[7] > 0){
        mem->arr_data_d = calloc(total_sizes_table[7], data_size(7));
        if(!mem->arr_data_d){goto error;}
    }
    if(mem->arr_cnt[8] > 0){
        mem->arr_data_b = calloc(total_sizes_table[8], data_size(8));
        if(!mem->arr_data_b){goto error;}
    }
    size_t arr_data_offset[TYPES_COUNT] = {0};
    size_t arr_index[TYPES_COUNT] = {0};
    LOG_I(TAG, "Now setting up arrays");
    for (size_t i = start_index; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (_parse_check_arr_header(data, &step) && _parse_check_arr_packet_size(len, step)) {
            for (size_t j = HEADER_SIZE; j < len; j += step) {
                data_types_t type = (data_types_t)data[j];
                size_t elem_count = data[j + 1];
                if (step >= 3) elem_count *= data[j + 2];
                if (step == 4) elem_count *= data[j + 3];
                size_t bytes_needed = elem_count * data_size(type);

                switch (type) {
                    //setup pointer for each array start [0] in given base array table 
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI8,  arr_ui8,  uint8_t,  mem->arr_data_ui8);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI16, arr_ui16, uint16_t, mem->arr_data_ui16);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_UI32, arr_ui32, uint32_t, mem->arr_data_ui32);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I8,   arr_i8,   int8_t,   mem->arr_data_i8);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I16,  arr_i16,  int16_t,  mem->arr_data_i16);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_I32,  arr_i32,  int32_t,  mem->arr_data_i32);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_F,    arr_f,    float,    mem->arr_data_f);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_D,    arr_d,    double,   mem->arr_data_d);
                    HANDLE_ARR_DATA_TYPE(mem, DATA_B,    arr_b,    bool,     mem->arr_data_b);
                    default:
                        ESP_LOGE(TAG, "Unknown array data type");
                        res.code = EMU_ERR_MEM_INVALID_DATATYPE;
                        res.warning = true;
                        goto error;

                }
            }
        }
    }
    LOG_I(TAG, "Array variables dataholder created successfully");
    return res;

    error: 
        if (res.code == EMU_OK){//so we do not need to set error everywhere where mem error
            res.code = EMU_ERR_NO_MEM;
            res.abort = true;
        }else{
            LOG_E(TAG, "Failed to create memory for arrays error: %s", EMU_ERR_TO_STR(res.code));
            emu_variables_reset(mem);
        }
        return res;
}


void emu_variables_reset(emu_mem_t *mem)
{
    if (!mem) return;
    if(mem->u8) free(mem->u8);
    if(mem->u16) free(mem->u16);
    if(mem->u32) free(mem->u32);
    if(mem->i8) free(mem->i8);
    if(mem->i16) free(mem->i16);
    if(mem->i32) free(mem->i32);
    if(mem->f) free(mem->f);
    if(mem->d) free(mem->d);
    if(mem->b) free(mem->b);
    if(mem->arr_data_ui8) free(mem->arr_data_ui8);
    if(mem->arr_data_ui16) free(mem->arr_data_ui16);
    if(mem->arr_data_ui32) free(mem->arr_data_ui32);
    if(mem->arr_data_i8) free(mem->arr_data_i8);
    if(mem->arr_data_i16) free(mem->arr_data_i16);
    if(mem->arr_data_i32) free(mem->arr_data_i32);
    if(mem->arr_data_f) free(mem->arr_data_f);
    if(mem->arr_data_d) free(mem->arr_data_d);
    if(mem->arr_data_b) free(mem->arr_data_b);
    if(mem->_base_arr_handle_ptr) free(mem->_base_arr_handle_ptr);
    *mem = (emu_mem_t){0};
    LOG_I(TAG, "Dataholder memory freed");
}


#define PACKET_SINGLE_VAR_SIZE 11  //2 + 9 * type cnt
/*********************ARR cheks*****************/
#define STEP_ARR_1D 2 // type, dim1
#define STEP_ARR_2D 3 // type, dim1, dim2
#define STEP_ARR_3D 4 // type, dim1, dim2, dim3
bool _parse_check_arr_header(uint8_t *data, uint8_t *step)
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

emu_result_t emu_parse_variables(chr_msg_buffer_t *source, emu_mem_t *mem)
{
    emu_result_t res = {.code = EMU_OK};
    uint8_t *data;
    size_t len;
    size_t start_index = -1;
    size_t buff_size = chr_msg_buffer_size(source);
    emu_err_t err = EMU_OK;
    
    LOG_I(TAG, "Counting total count of scalars");
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
                ESP_LOGI(TAG, "type %s: count %d", DATA_TYPE_TO_STR[j], mem->single_cnt[j]);
            }
            break;  
        }
    }
    if (start_index == -1)
    {
        ESP_LOGE(TAG, "Scalar variables packet not found can't create memory");
        res.code = EMU_ERR_NO_MEM;
        res.abort = true;
        return res;
    }
    //create space for single variables and create pointers
    res = emu_variables_single_create(mem);
    if (res.code != EMU_OK){
        ESP_LOGE(TAG, "When creating single variables error: %s", EMU_ERR_TO_STR(res.code));
        res.abort = true;
        return res;
    }
    res = emu_variables_arrays_create(source, mem, start_index);
    if (res.code != EMU_OK){
        ESP_LOGE(TAG, "When creating arrays error: %s", EMU_ERR_TO_STR(res.code));
        res.abort = true;
        return res;
    }
    return res;
}

#define ARR_DATA_START_IDX 5
/*parse switch case generator for handling different types of arrays that needs to be filled*/
/**/
#define HANDLE_ARRAY_CASE(HEADER, TYPE, MEMBER, DATA_LEN)                       \
    case HEADER: {                                                              \
        arr_##MEMBER##_t *arr = &mem->arr_##MEMBER[arr_index];                  \
        uint16_t total_size = arr->dims[0];                                     \
        for (uint8_t d = 1; d < arr->num_dims; d++) total_size *= arr->dims[d]; \
        total_size *= sizeof(TYPE);                                             \
        uint32_t byte_offset = (uint32_t)idx_offset * sizeof(TYPE);             \
        if ((byte_offset + DATA_LEN) <= total_size) {    \
            uint8_t *dest = (uint8_t *)arr->data + byte_offset;                 \
            memcpy(dest, &data[ARR_DATA_START_IDX], (DATA_LEN));                \
        } else {                                                                \
            ESP_LOGW(TAG, "Out of bounds " #TYPE " idx %d, start idx %d, target size %d, provided size %d",\
                 arr_index, idx_offset, total_size, DATA_LEN);\
        }                                                                       \
        break;                                                                  \
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

emu_result_t emu_parse_variables_into(chr_msg_buffer_t *source, emu_mem_t *mem) {
    emu_result_t res = {.code = EMU_OK};
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    for (uint16_t i = 0; i < buff_size; ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len < HEADER_SIZE) continue;
        //at idx is arr idx as we know the type
        uint8_t  arr_index = data[HEADER_SIZE];
        /*offset is for when array only needs to be filled from certain point so we don't need 
        padding as we want to set only certain value */
        uint16_t idx_offset    = READ_U16(data,3); 
        uint16_t arr_bytes_to_copy = len - ARR_DATA_START_IDX;

        /*switch header type*/
        emu_header_t header= (emu_header_t)(GET_HEADER(data));
        switch (header) {
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_UI8,  uint8_t,  ui8,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_UI16, uint16_t, ui16, arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_UI32, uint32_t, ui32, arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_I8,   int8_t,   i8,   arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_I16,  int16_t,  i16,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_I32,  int32_t,  i32,  arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_F,    float,    f,    arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_D,    double,   d,    arr_bytes_to_copy)
            HANDLE_ARRAY_CASE(EMU_H_VAR_ARR_B,    bool,     b,    arr_bytes_to_copy)

            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_UI8,  uint8_t,  u8,  len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_UI16, uint16_t, u16, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_UI32, uint32_t, u32, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_I8,   int8_t,   i8,  len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_I16,  int16_t,  i16, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_I32,  int32_t,  i32, len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_F,    float,    f,   len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_D,    double,   d,   len)
            HANDLE_SINGLE_CASE(EMU_H_VAR_SCAL_B,    bool,     b,   len)
            default:
                break;
        }
    }
    return res;
}