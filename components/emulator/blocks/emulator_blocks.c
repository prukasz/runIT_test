#include "emulator_blocks.h"
#include "emulator_variables.h"
#include "utils_block_in_q_access.h"
#include "utils_global_access.h"
#include "emulator_body.h"
#include "utils_parse.h"
#include <math.h>

#define HEADER_SIZE 2

extern void **emu_block_struct_execution_list;

void block_pass_results(block_handle_t* block)
{
    if (!block || !block->q_connections_table) return;

    for (uint8_t q_idx = 0; q_idx < block->q_cnt; q_idx++) {
        void* src = (uint8_t*)block->q_data + block->q_data_offsets[q_idx];
        data_types_t src_type = block->q_data_type_table[q_idx];

        for (uint8_t conn_idx = 0; conn_idx < block->q_connections_table[q_idx].conn_cnt; conn_idx++) {
            block_handle_t* target_block = emu_block_struct_execution_list[block->q_connections_table[q_idx].target_blocks_id_list[conn_idx]];
            if (!target_block) continue;

            uint8_t target_input = block->q_connections_table[q_idx].target_inputs_list[conn_idx];
            void* dst = (uint8_t*)target_block->in_data + target_block->in_data_offsets[target_input];
            data_types_t dst_type = target_block->in_data_type_table[target_input];
            SET_BIT16N(target_block->in_set, target_input);
            double val = 0.0;
            switch (src_type) {
                case DATA_UI8:  { uint8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_UI16: { uint16_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_UI32: { uint32_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I8:   { int8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I16:  { int16_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I32:  { int32_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_F:    { float tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_D:    { memcpy(&val, src, sizeof(double)); } break;
                case DATA_B:    { uint8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp != 0; } break;
                default: ESP_LOGE("COPY_DBG","unknown src type %d", src_type); continue;
            }

            if (!(dst_type == DATA_F || dst_type == DATA_D)) val = round(val);

            switch (dst_type) {
                case DATA_UI8:  { if (val < 0) val = 0; if (val > UINT8_MAX) val = UINT8_MAX; *(uint8_t*)dst = (uint8_t)val; } break;
                case DATA_UI16: { if (val < 0) val = 0; if (val > UINT16_MAX) val = UINT16_MAX; *(uint16_t*)dst = (uint16_t)val; } break;
                case DATA_UI32: { if (val < 0) val = 0; if (val > UINT32_MAX) val = UINT32_MAX; *(uint32_t*)dst = (uint32_t)val; } break;
                case DATA_I8:   { if (val < INT8_MIN) val = INT8_MIN; if (val > INT8_MAX) val = INT8_MAX; *(int8_t*)dst = (int8_t)val; } break;
                case DATA_I16:  { if (val < INT16_MIN) val = INT16_MIN; if (val > INT16_MAX) val = INT16_MAX; *(int16_t*)dst = (int16_t)val; } break;
                case DATA_I32:  { if (val < INT32_MIN) val = INT32_MIN; if (val > INT32_MAX) val = INT32_MAX; *(int32_t*)dst = (int32_t)val; } break;
                case DATA_F:    { *(float*)dst = (float)val; } break;
                case DATA_D:    { memcpy(dst, &val, sizeof(double)); } break;
                case DATA_B:    { *(uint8_t*)dst = (val != 0.0) ? 1 : 0; } break;
                default: ESP_LOGE("COPY_DBG","unknown dst type %d", dst_type); break;
            }
        }
    }
}

static const char *TAG_PARSE = "BLOCK ALLOCATION";
void free_single_block(block_handle_t* block) {
    if (!block) return;

    
    // Free input-related memory
    if (block->in_data_type_table) free(block->in_data_type_table);
    if (block->in_data_offsets) free(block->in_data_offsets);
    if (block->in_data) free(block->in_data);

    // Free output-related memory
    if (block->q_data_type_table) free(block->q_data_type_table);
    if (block->q_data_offsets) free(block->q_data_offsets);
    if (block->q_data) free(block->q_data);

    // Free connections table
    if (block->q_connections_table && block->q_cnt) {
        for (uint8_t q = 0; q < block->q_cnt; q++) {
            q_connection_t* conn = &block->q_connections_table[q];
            if (conn->target_blocks_id_list) free(conn->target_blocks_id_list);
            if (conn->target_inputs_list) free(conn->target_inputs_list);
        }
        free(block->q_connections_table);
        if(block->extras){
            switch (block->block_type)
            {
            case BLOCK_COMPUTE:
                //dummy (free content)
                break;
            default:
                break;
            }
        }     
    }
    utils_global_var_access_free(block->global_reference, block->global_reference_cnt);
    free(block);
}



static block_handle_t *_block_create_struct(uint8_t in_cnt, uint8_t q_cnt){
    block_handle_t *block = calloc(1, sizeof(block_handle_t));
    if(!block){return NULL;}
    block -> in_cnt = in_cnt;
    block -> q_cnt  = q_cnt;
    block -> in_data_offsets     = calloc(in_cnt, sizeof(uint16_t));
    block -> q_data_offsets      = calloc(q_cnt,  sizeof(uint16_t));
    block->in_data_type_table    = calloc(in_cnt, sizeof(data_types_t));
    block->q_data_type_table     = calloc(q_cnt,  sizeof(data_types_t));
    block->q_connections_table   = calloc(q_cnt,  sizeof(q_connection_t));
    if(! block -> in_data_offsets||!block -> q_data_offsets||!block->in_data_type_table||
        !block->q_data_type_table||!block->q_connections_table){
            ESP_LOGE(TAG_PARSE, "Block allocation failed");
            free_single_block(block);
            return NULL;
        }
    return block;
}

void emu_blocks_free_all(block_handle_t** blocks_list, uint16_t blocks_cnt) {
    ESP_LOGI("BLOCKS", "Blocks cleared");
    if (!blocks_list) {

        LOG_E(TAG_PARSE, "No block list provided to free");
        return;
    }
    for (size_t i = 0; i < blocks_cnt; i++) {
        LOG_I(TAG_PARSE, "Now will free block %d", i);
        free_single_block(blocks_list[i]);
    }
    if(blocks_list){free(blocks_list);}
}


emu_err_t emu_parse_total_block_cnt(chr_msg_buffer_t *source){
    uint16_t cnt = 0;
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx =0;
    while (search_idx < buff_size){
        chr_msg_buffer_get(source, search_idx, &data, &len);
            if(parse_check_header(data, EMU_H_BLOCK_CNT)){
                cnt = READ_U16(data, HEADER_SIZE);
                ESP_LOGI("BLOCK_COUNT_PARSER", "Found %d block structs to allocate", cnt);
                return emu_body_create_execution_table(cnt);
            }
        search_idx++;
    }
    return EMU_ERR_NOT_FOUND;
}


#define IN_Q_STEP 3  //block id (2) + in num (1)
emu_err_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t ** blocks_list, uint16_t blocks_total_cnt)
{
    static const char* TAG  = "BLOCK STRUCT PARSE";
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint8_t search_idx = 0;
    uint16_t block_idx = 0;

    while (search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[2] == 0x00)
        {
            LOG_I(TAG, "Dected start header of block type: %d", data[1]);

            block_idx = READ_U16(data, 3);
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

            block_handle_t *block_new = _block_create_struct(in_cnt, q_cnt);
            if(!block_new){
                ESP_LOGE(TAG, "Memory error during allocation of block");
                return EMU_ERR_NO_MEM;
            }
            block_new->block_idx = block_idx;
            block_new->block_type = data[1];
            blocks_list[block_idx]=  block_new;
            

            // PARSE INPUT TYPES

            uint16_t total_in_size = 0;
            for (uint8_t i = 0; i < in_cnt; i++)
            {
                block_new->in_data_offsets[i] = total_in_size;
                data_types_t type = data[in_types_start + i]; 
                block_new->in_data_type_table[i] = type;
                total_in_size += data_size(type);
            }

            block_new->in_data = calloc(total_in_size, sizeof(uint8_t));
            if(!block_new->in_data && (block_new-> in_cnt > 0)){
                ESP_LOGE(TAG, "No memory to allocate inputs of block %d", block_idx);
                emu_blocks_free_all(blocks_list, blocks_total_cnt);
                return EMU_ERR_NO_MEM;
            }
            // PARSE Q TYPES

            uint16_t q_types_start = idx;

            uint16_t total_q_size = 0;
            for (uint8_t i = 0; i < q_cnt; i++)
            {
                block_new->q_data_offsets[i] = total_q_size;
                data_types_t type = data[q_types_start + i];
                block_new->q_data_type_table[i] = type;
                total_q_size += data_size(type);
            }
            idx += q_cnt;    // move idx to q_used
            block_new->q_data = calloc(total_q_size, sizeof(uint8_t));
            if(!block_new->q_data && (block_new-> q_cnt > 0)){
                ESP_LOGE(TAG, "No memory to allocate outputs of block %d", block_idx);
                emu_blocks_free_all(blocks_list, blocks_total_cnt);
                return EMU_ERR_NO_MEM;
            }

            ESP_LOGI(TAG, "block block_idx:%d  has: %d inputs and %d outputs", block_idx, in_cnt, q_cnt);

            // PARSE Q -> CONNECTIONS

            uint8_t q_used = data[idx];
            idx++;
            uint8_t total_lines = 0;
            for (uint8_t q = 0; q < q_used; q++)
            {
                uint8_t conn_cnt = data[idx];
    
                block_new->q_connections_table[q].conn_cnt = conn_cnt;
                block_new->q_connections_table[q].target_blocks_id_list = (uint16_t *)calloc(conn_cnt, sizeof(uint16_t));
                block_new->q_connections_table[q].target_inputs_list = (uint8_t *)calloc(conn_cnt, sizeof(uint8_t));
                if((!block_new->q_connections_table[q].target_blocks_id_list ||!block_new->q_connections_table[q].target_inputs_list)&&conn_cnt !=0){
                    ESP_LOGE(TAG, "No memory to allocate output connections for block %d", block_idx);
                    emu_blocks_free_all(blocks_list, blocks_total_cnt);
                    return EMU_ERR_NO_MEM;
                }
                idx++; 
                for (uint8_t conn = 0; conn < conn_cnt; conn++)
                {
                    total_lines ++; 
                    block_new->q_connections_table[q].target_blocks_id_list[conn] = READ_U16(data, idx);
                    block_new->q_connections_table[q].target_inputs_list[conn] = data[idx + 2];

                    ESP_LOGI(TAG, "Q number:%d |line %d| target block_idx:%d, target in:%d",
                             q, total_lines,
                             block_new->q_connections_table[q].target_blocks_id_list[conn],
                             block_new->q_connections_table[q].target_inputs_list[conn]);
                    idx += IN_Q_STEP;// move to next line 
                }
            }
            if(total_lines == 0){
                LOG_I(TAG, "No connections form block_idx: %d", block_new->block_idx);
            }
            LOG_I(TAG, "Parsing global access");
            utils_parse_global_access_for_block(source, search_idx, block_new);
            utils_parse_fuction_assign_to_block(block_new);
            LOG_I(TAG, "Allocated handle for block block_idx %d", block_new->block_idx);
        }
        search_idx++;
    }
    return EMU_OK;
}






