#include "emulator_blocks.h"
#include "emulator_variables.h"
#include "utils_block_in_q_access.h"
#include "utils_global_access.h"
#include "emulator_body.h"
#include "utils_parse.h"
#include <math.h>
#include "blocks_all_list.h"
#include "emulator_errors.h"


#define HEADER_SIZE 2

extern void **emu_block_struct_execution_list;

void block_pass_results(block_handle_t* block)
{
    static const char* TAG = "BLOCK_PASS_RESULTS";
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
                default: ESP_LOGE(TAG,"unknown src type %d", src_type); continue;
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
                default: ESP_LOGE(TAG,"unknown dst type %d", dst_type); break;
            }
        }
    }
}

static const char *TAG_PARSE = "BLOCK_PARSE";
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
            case BLOCK_MATH:
                emu_math_block_free_expression(block);
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
    /* if inputs or outputs provided alocate space for them */
    if (in_cnt > 0 ){
        block -> in_data_offsets    = calloc(in_cnt, sizeof(uint8_t));
        block -> in_data_type_table = calloc(in_cnt, sizeof(data_types_t));
        if(!block ->in_data_offsets || !block -> in_data_type_table)goto error;
    }
    if (q_cnt > 0 ){
        block -> q_data_offsets    = calloc(in_cnt, sizeof(uint8_t));
        block -> q_data_type_table = calloc(in_cnt, sizeof(data_types_t));
        block -> q_connections_table = calloc(q_cnt,  sizeof(q_connection_t));
        if(!block ->in_data_offsets || !block -> in_data_type_table || !block->q_connections_table )goto error;
    }
    return block;
    /*if any failed, clear block*/
    error: 
    LOG_E(TAG_PARSE, "(_block_create_struct), stuct allocation failed");
    free_single_block(block);
    return NULL;
}

void emu_blocks_free_all(block_handle_t** blocks_list, uint16_t blocks_cnt) {
    ESP_LOGI(TAG_PARSE, "%d blocks cleared", blocks_cnt);
    if (!blocks_list) {
        LOG_E(TAG_PARSE, "No block list provided to clear");
        return;
    }
    for (size_t i = 0; i < blocks_cnt; i++) {
        LOG_I(TAG_PARSE, "Now will free block %d", i);
        free_single_block(blocks_list[i]);
    }
    if(blocks_list){free(blocks_list);}
}


emu_result_t emu_parse_total_block_cnt(chr_msg_buffer_t *source){
    emu_result_t res = {.code = EMU_OK};
    uint16_t cnt = 0;
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    size_t search_idx =0;
    while (search_idx < buff_size){
        chr_msg_buffer_get(source, search_idx, &data, &len);
            if(parse_check_header(data, EMU_H_BLOCK_CNT)){
                cnt = READ_U16(data, HEADER_SIZE);
                ESP_LOGI(TAG_PARSE, "Found %d block structs to allocate", cnt);
                res = emu_body_create_execution_table(cnt);
                return res;
            }
        search_idx++;
    }
    res.code = EMU_ERR_NOT_FOUND;
    res.abort = true;
    return res;
}


#define IN_Q_STEP 3  //block id (2) + in num (1)
#define DATA_START 5
emu_result_t emu_parse_block(chr_msg_buffer_t *source, block_handle_t ** blocks_list, uint16_t blocks_total_cnt)
{
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);
    uint8_t search_idx = 0;
    uint16_t block_idx = 0;
    block_handle_t * block_new = NULL;
    emu_result_t res = {.code = EMU_OK};
    while (search_idx < buff_size)
    {
        chr_msg_buffer_get(source, search_idx, &data, &len);

        if (data[0] == 0xbb && data[2] == 0x00)
        {
            LOG_I(TAG_PARSE, "Dected header of block type: %d", data[1]);

            block_idx = READ_U16(data, 3);

            uint16_t idx = DATA_START;
            uint16_t in_used = READ_U16(data, idx); //used inputs bitmask
            idx+=sizeof(uint16_t);
            uint8_t in_cnt = data[idx];             //input count
            idx++;
            uint8_t in_size = data[idx];            //input size (bytes)
            idx++; 
            uint16_t idx_in_list = idx;             
            idx += in_cnt;
            uint8_t q_cnt  = data[idx];             //output count
            idx++;
            uint8_t q_size = data[idx];             //output size (bytes)
            idx++;
            uint16_t idx_q_list = idx;
            idx += q_cnt;

            block_new = _block_create_struct(in_cnt, q_cnt);
            if(!block_new){
                res.code = EMU_ERR_NO_MEM;
                res.restart = true;
                goto error;
            }

            blocks_list[block_idx] = block_new;
            block_new->block_idx   = block_idx;
            block_new->block_type  = data[1];
            block_new->in_used     = in_used;
            
            ESP_LOGI(TAG_PARSE, "block block_idx:%d  has: %d inputs and %d outputs", block_idx, in_cnt, q_cnt);
            if (in_size > 0){
                block_new->in_data = calloc(in_size, sizeof(uint8_t));
                if(!block_new->in_data){
                    LOG_E(TAG_PARSE, "Error when allocating space for in data (%d)B", in_size);
                    res.code = EMU_ERR_NO_MEM;
                    res.restart = true;
                    goto error;
                }
            }
            if (q_size > 0){
                block_new->q_data = calloc(in_size, sizeof(uint8_t));
                if(!block_new->q_data){
                    LOG_E(TAG_PARSE, "Error when allocating space for in data (%d)B", q_size);
                    res.code = EMU_ERR_NO_MEM;
                    res.restart = true;
                    goto error;
                }
            }

            /*setup inputs and inputs offsets*/
            memcpy(block_new->in_data_type_table, &data[idx_in_list], in_cnt);
            uint16_t current_in_offset = 0;
            for (uint8_t in = 0; in < in_cnt; in++) {
                LOG_I(TAG_PARSE, "In %d: %s", in, DATA_TYPE_TO_STR[block_new->in_data_type_table[in]]);
                block_new->in_data_offsets[in] = current_in_offset;
                current_in_offset += data_size(block_new->in_data_type_table[in]);
            }
            /*setup outputs and outputs offsets*/
            memcpy(block_new->q_data_type_table, &data[idx_q_list], q_cnt);
            uint16_t current_q_offset = 0;
            for (uint8_t q = 0; q < q_cnt; q++) {
                LOG_I(TAG_PARSE, "Q %d: %s", q, DATA_TYPE_TO_STR[block_new->q_data_type_table[q]]);
                block_new->q_data_offsets[q] = current_q_offset;
                current_q_offset += data_size(block_new->q_data_type_table[q]);
            }
 
            uint8_t q_conn_provided = data[idx]; //check how many provided
            idx++;

            uint8_t total_lines = 0;
            for (uint8_t q = 0; q < q_conn_provided; q++)
            {
                uint8_t conn_cnt = data[idx];
    
                block_new->q_connections_table[q].conn_cnt = conn_cnt;
                block_new->q_connections_table[q].target_blocks_id_list = (uint16_t *)calloc(conn_cnt, sizeof(uint16_t));
                block_new->q_connections_table[q].target_inputs_list = (uint8_t *)calloc(conn_cnt, sizeof(uint8_t));
                if((!block_new->q_connections_table[q].target_blocks_id_list ||!block_new->q_connections_table[q].target_inputs_list)&&conn_cnt > 0){
                    ESP_LOGE(TAG_PARSE, "No memory to allocate output connections for block %d", block_idx);
                    res.code = EMU_ERR_NO_MEM;
                    res.restart = true;
                    goto error;
                }
                idx++; 
                for (uint8_t conn = 0; conn < conn_cnt; conn++)
                {
                    total_lines ++; 
                    block_new->q_connections_table[q].target_blocks_id_list[conn] = READ_U16(data, idx);
                    block_new->q_connections_table[q].target_inputs_list[conn] = data[idx + 2];

                    ESP_LOGI(TAG_PARSE, "Q number:%d |line %d| target block_idx -> %d, target in [%d]",
                             q, total_lines,
                             block_new->q_connections_table[q].target_blocks_id_list[conn],
                             block_new->q_connections_table[q].target_inputs_list[conn]);
                    idx += IN_Q_STEP;// move to next 'line'
                }
            }
            if(total_lines == 0){
                LOG_I(TAG_PARSE, "No connections form block_idx: %d", block_new->block_idx);
            }
            res = utils_parse_global_access_for_block(source, search_idx, block_new);
            if(EMU_OK != res.code){
                LOG_E(TAG_PARSE, "When creating global references error: %s", EMU_ERR_TO_STR(res.code));
                res.code = EMU_ERR_NO_MEM;
                res.restart = true;
                goto error;
            }
            res = utils_parse_fuction_assign_to_block(block_new);
            if(EMU_OK != res.code){
                LOG_E(TAG_PARSE, "When assigning function to block error: %s", EMU_ERR_TO_STR(res.code));
                res.code = EMU_ERR_NO_MEM;
                res.restart = true;
                goto error;
            }
        }
        search_idx++;
    }
    
    return res;
    error:
        ESP_LOGE(TAG_PARSE, "Error during block creation: %s, block %d won't be created", EMU_ERR_TO_STR(res.code), data[1]);
        free(block_new);
    return res;
}

// Prototypes for specific block verification functions (defined in their respective .c files)
extern emu_result_t emu_parse_verify_block_math(void* extras);
extern emu_result_t emu_parse_verify_block_logic(void* extras);
extern emu_result_t emu_parse_verify_block_for(void* extras);
// ... other prototypes ...

emu_result_t emu_parse_verify_blocks(block_handle_t **blocks_list, uint16_t total_blocks_cnt) {
    emu_result_t res = {.code = EMU_OK};

    static const char * TAG = "BLOCK VERIFY";
    if (!blocks_list){
        LOG_I(TAG, "No block list provided");
        res.code = EMU_ERR_NULL_PTR;
        res.restart = true;
        return res;
    }

    for (uint16_t i = 0; i < total_blocks_cnt; i++) {
        block_handle_t* block = blocks_list[i];
       
        if (!block){
            LOG_E(TAG, "Block %d isn't created", i);
            res.code = EMU_ERR_NULL_PTR;
            res.restart = true;
            return res;
        }
        // Iterate through all outputs of this block
        for (uint8_t q_idx = 0; q_idx < block->q_cnt; q_idx++) {
            
            q_connection_t* conn_info = &block->q_connections_table[q_idx];

            for (uint8_t wire_idx = 0; wire_idx < conn_info->conn_cnt; wire_idx++) {
                
                uint16_t target_id = conn_info->target_blocks_id_list[wire_idx];
                uint8_t  target_in = conn_info->target_inputs_list[wire_idx];

                if (target_id >= total_blocks_cnt || blocks_list[target_id] == NULL) {
                    res.code = EMU_ERR_BLOCK_INVALID_CONN;
                    res.restart = true;
                    return res;
                }

                block_handle_t* target_block = blocks_list[target_id];
                if (target_in >= target_block->in_cnt) {
                    LOG_I(TAG, "Target in of block %d, index %d doesn't exist in block %d", block->block_idx, target_in, target_id);
                    res.code = EMU_ERR_BLOCK_INVALID_CONN;
                    res.restart = true;
                    return res;
                }
            }
        }
    }

    // for (uint16_t i = 0; i < total_blocks_cnt; i++) {
    //     block_handle_t* block = blocks_list[i];

    //     if (block->extras != NULL) {
    //         emu_err_t extras_res = EMU_OK;

    //         switch (block->block_type) {
    //             case BLOCK_MATH:
    //                 extras_res = emu_parse_verify_block_math(block->extras);
    //                 break;
                
    //             case BLOCK_CMP: 
    //                 extras_res = emu_parse_verify_block_logic(block->extras);
    //                 break;

    //             case BLOCK_FOR:
    //                 extras_res = emu_parse_verify_block_for(block->extras);
    //                 break;

    //             case BLOCK_SET_GLOBAL:
    //                 // Example: check if global ref exists
    //                 // extras_res = emu_parse_verify_block_global_set(block->extras);
    //                 break;

    //             default:
    //                 // If a block has extras but no case here, is it an error?
    //                 // Usually OK to ignore if no deep verification needed.
    //                 break;
    //         }

    //         if (extras_res != EMU_OK) {
    //             return extras_res;
    //         }
    //     }
    // }

    return res;
}
