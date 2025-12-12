#include "utils_global_access.h"
#include "emulator_variables.h"
#include "emulator_errors.h"
#include "emulator_blocks.h"
#include "utils_parse.h"
#include "stdbool.h"
#include "esp_log.h"
#include "math.h"
/****************************************************************************************************** */
#define ACCES_LINK_STEP 5 //len of(type + idx + 3 x static indices)
#define ACCES_LINK_NO_CHILD 0xFF
const static char* TAG = "GLOBAL VARIABLE ACCESS";
static global_acces_t* _link_recursive(uint8_t *data, uint16_t *cursor, uint16_t len, global_acces_t *pool, uint16_t *pool_idx, uint16_t pool_max_size);
/****************************************************************************************************** */

emu_err_t utils_global_var_acces_recursive(global_acces_t *root, double *out){
    if (!out){
        LOG_E(TAG, "No output provided to return result ");
        return EMU_ERR_NULL_PTR;
    }else if (!root){
        LOG_E(TAG, "No access struct provided");
        return EMU_ERR_NULL_PTR;
    }

    uint8_t idx_table[MAX_ARR_DIMS];
    global_acces_t *next_arr[MAX_ARR_DIMS] = {root->next0, root->next1, root->next2};
    for (uint8_t i = 0; i < MAX_ARR_DIMS; i++){

        if (NULL!=next_arr[i]) {
            double tmp;
            EMU_RETURN_ON_ERROR(utils_global_var_acces_recursive(next_arr[i], &tmp));

            if (tmp < 0.0 ||(tmp > 0xFE)) {
                ESP_LOGE(TAG, "Provided indices out of range (0, 254)");
                return EMU_ERR_MEM_INVALID_IDX;
            }else{//cast and round to ensure safety
                idx_table[i] = (uint8_t)round(tmp);}
        } else {
            idx_table[i] = root->target_custom_indices[i];
            LOG_I(TAG, "Using static index %d: %d", i, idx_table[i]);
        }
    }
    //check if provided static indices tells that varaible is scalar
    bool is_scalar = (idx_table[0] == UINT8_MAX && idx_table[1] == UINT8_MAX &&idx_table[2] == UINT8_MAX);

    LOG_I(TAG, "DEBUG: %s %u scalar? %d indices:[%d,%d,%d]",
            DATA_TYPE_TO_STR[root->target_type], root->target_idx, is_scalar,
            idx_table[0], idx_table[1], idx_table[2]);

    //check if variable exists (is it necessary?)
    if (is_scalar) {
        if (root->target_idx >= mem.single_cnt[root->target_type]) {
            LOG_E(TAG, "Scalar index out of range (probably don't exist)");
            return EMU_ERR_MEM_INVALID_IDX;
        }
    } else {
        if (root->target_idx >= mem.arr_cnt[root->target_type]) {
            LOG_E(TAG, "Array index out of range (probably don't exist)");
            return EMU_ERR_MEM_INVALID_IDX;
        }
    }
    double val;

    emu_err_t err = mem_get_as_d(root->target_type, root->target_idx, idx_table, &val);
    if(EMU_OK!=err){
        LOG_E(TAG, "While trying to access variable type %s, at idx %d and indices %d, %d, %d encountered err: %s",
            DATA_TYPE_TO_STR[root->target_type], root->target_idx, idx_table[0], idx_table[1], idx_table[3], EMU_ERR_TO_STR(err));
            return err;
    }


    *out = val;
    return EMU_OK;
}



void utils_global_var_access_free(global_acces_t** access_list, uint8_t cnt){
    if (access_list == NULL) return;

    for(uint8_t i = 0; i < cnt; i++){
        // We just free the head of the block.
        // No recursion needed. No children freeing needed.
        if (access_list[i] != NULL) {
            free(access_list[i]); 
            access_list[i] = NULL;
        }
    }

    // Free the array that held the pointers
    free(access_list);
}

emu_err_t utils_parse_global_access_for_block(chr_msg_buffer_t *source, uint16_t start, block_handle_t *block) {
    uint8_t *data;
    size_t len;
    size_t buffer_len = chr_msg_buffer_size(source);
    uint8_t total_references = 0;
    uint8_t cursor = start;

    if (!block || ! source){
        LOG_E(TAG, "Null block ptr or NULLsource ptr");
        return EMU_ERR_NULL_PTR;
    }
    //check total count of global access
    while(cursor < buffer_len){ 
        chr_msg_buffer_get(source, cursor, &data, &len);
        if(data[0] == EMU_H_BLOCK_PACKET && data[2] >= EMU_H_BLOCK_START_G_ACCES && READ_U16(data,3) == block->block_idx){
            total_references++;
        }
        cursor++;
    }
    block->global_reference_cnt = total_references;


    
    // Allocate the array of pointers (pointers to the trees)
    block->global_reference = (global_acces_t**)calloc(total_references, sizeof(global_acces_t*));
    if(NULL == block->global_reference && total_references !=0 ){
        LOG_E(TAG, "Array allocation failed");
        return EMU_ERR_NO_MEM;
    }

    //Now allocate pool and link all nodes
    cursor = start;
    uint8_t current_ref_idx = 0; 
    
    while(cursor < buffer_len){
        chr_msg_buffer_get(source, cursor, &data, &len);
        
        if(data[0] == EMU_H_BLOCK_PACKET && data[2] >= EMU_H_BLOCK_START_G_ACCES && READ_U16(data,3) == block->block_idx){
            // 1. Calculate size needed for THIS specific tree
            uint16_t total_nodes = 1; 
            uint16_t check_idx = 10; 
            while (check_idx < len) {
                if (data[check_idx] == 0xFF) {
                    check_idx++;
                } else {
                    total_nodes++;
                    check_idx += ACCES_LINK_STEP;
                }
            }
            
            
            global_acces_t *pool = calloc(total_nodes, sizeof(global_acces_t));
            if (!pool) {
                LOG_E(TAG, "Poll allocation failed for global access");
                return EMU_ERR_NO_MEM;
            }

            uint16_t packet_idx = 5;      
            uint16_t nodes_used = 0;  
            //connect all created nodes in pool table
            _link_recursive(data, &packet_idx, len, pool, &nodes_used, total_nodes);
            //assign pool to reference tabe
            //pool first element is root so when we access at idx we get root ptr
            block->global_reference[current_ref_idx] = pool;
            current_ref_idx++;
        }
        cursor++;
    }

    ESP_LOGI(TAG, "Created %d global references for block %d", current_ref_idx, block->block_idx);
    return EMU_OK;
}

/**
 * @brief connect and fill nodes from one global access
 */
static global_acces_t* _link_recursive(uint8_t *data, uint16_t *cursor, uint16_t len, global_acces_t *pool, uint16_t *pool_idx, uint16_t pool_max_size)
{
    if (*cursor >= len){return NULL;}
    if (*pool_idx >= pool_max_size) {
        LOG_E(TAG, "Pool overflow! Calculated %d nodes but tried to use more", pool_max_size);
        return NULL; 
    }

    global_acces_t *node = &pool[*pool_idx];
    (*pool_idx)++;

    //parse data into one of selected pool
    node->target_type = data[*cursor];
    node->target_idx  = data[*cursor + 1];
    node->target_custom_indices[0] = data[*cursor + 2];
    node->target_custom_indices[1] = data[*cursor + 3];
    node->target_custom_indices[2] = data[*cursor + 4];
    *cursor += ACCES_LINK_STEP;; 

    //check if child 0 exist
    if (*cursor < len && data[*cursor] == ACCES_LINK_NO_CHILD) {
        node->next0 = NULL;
        (*cursor)++; 
    } else {
        node->next0 = _link_recursive(data, cursor, len, pool, pool_idx, pool_max_size);
    }

    //check if child 0 exist
    if (*cursor < len && data[*cursor] == ACCES_LINK_NO_CHILD) {
        node->next1 = NULL;
        (*cursor)++;
    } else {
        node->next1 = _link_recursive(data, cursor, len, pool, pool_idx, pool_max_size);
    }

    //check if child 0 exist
    if (*cursor < len && data[*cursor] == ACCES_LINK_NO_CHILD) {
        node->next2 = NULL;
        (*cursor)++;
    } else {
        node->next2 = _link_recursive(data, cursor, len, pool, pool_idx, pool_max_size);
    }
    return node;
}