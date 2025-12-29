#include "gatt_buff.h"
#include "gatt_svc.h"
#include "emulator_debug.h"
#include "emulator_interface.h"
#include "emulator_types.h"
#include "esp_log.h"
#include "emulator_variables.h"

static const char *TAG = "EMU_DEBUG";

chr_msg_buffer_t *output;


emu_result_t debug_blocks_value_dump(block_handle_t** block_struct_list, uint16_t total_block_cnt, uint16_t mtu)
{
    //mtu is negotiated by peer 
    emu_result_t res = {.code = EMU_OK};
    //static, cuz we don't need to call malloc, so less overhead and heap overfow?
    //we will surely need beggies size anyway
    static uint8_t my_data[DUMP_BUF_SIZE];
    memset(my_data, 0, sizeof(my_data));
    uint16_t idx = 0;

    if (NULL==output){
        LOG_W(TAG, "No output buffer provided for debug dump");
        res.code = EMU_ERR_NULL_PTR;
        res.warning = true;
        return res;   
    }
    uint16_t packet_num = 0;
    memcpy(&my_data[0], &packet_num, sizeof(uint16_t));
    idx += sizeof(uint16_t);

    for (uint16_t i = 0; i < total_block_cnt; i++) {
        uint16_t total_size = 0;
        block_handle_t *block = block_struct_list[i];
        total_size += sizeof(uint16_t);  //send block lenght
        total_size += (4*sizeof(uint16_t)+4*sizeof(uint8_t));
        uint8_t in_type_size = block->in_cnt;
        uint8_t q_type_size = block->q_cnt;
        uint8_t in_data_size = 0; 
        uint8_t q_data_size = 0;
        if(block->in_cnt>0){in_data_size = block->in_data_offsets[block->in_cnt-1]+data_size(block->in_data_type_table[block->in_cnt-1]);}
        if(block->q_cnt>0){q_data_size = block->q_data_offsets[block->q_cnt-1]+data_size(block->q_data_type_table[block->q_cnt-1]);}
        
        total_size += (in_type_size+q_type_size+in_data_size+q_data_size);
        if(idx + total_size  >= mtu){
            //if block don't fit in packet send it in next one
            chr_msg_buffer_add(output, my_data, idx);
            //clear buffer and get next packet ready
            idx = 0;
            memset(my_data, 0, sizeof(my_data));
            memcpy(&my_data[idx], &packet_num, sizeof(uint16_t));
            idx += sizeof(uint16_t); //packet num
            packet_num++;
        }
        total_size -=2; //remove len of packet len 
        memcpy(&my_data[idx], &total_size, sizeof(uint16_t));
        idx += sizeof(uint16_t);
        // 1. Block ID (uint16_t)
        memcpy(&my_data[idx], &block->block_idx, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 2. Block Type (uint8_t)
        uint8_t b_type = (uint8_t)block->block_type;
        memcpy(&my_data[idx], &b_type, sizeof(uint8_t));
        idx += sizeof(uint8_t);

                    /*INPUTS*/

        // 3. In Count (uint8_t)
        memcpy(&my_data[idx], &block->in_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);

        // 4. In Used Mask (uint16_t)
        memcpy(&my_data[idx], &block->in_used, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 5. In Set Mask (uint16_t)
        memcpy(&my_data[idx], &block->in_set, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 6. In Tables & data (uint8_t)
        if (block->in_cnt > 0) {
            memcpy(&my_data[idx], block->in_data_type_table, in_type_size);
            idx += in_type_size;
            memcpy(&my_data[idx], block->in_data, in_data_size);
            idx += in_data_size;
        }
        // 7. Q Count (uint8_t)
        memcpy(&my_data[idx], &block->q_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);
        

        // 8. Q Tables & data (uint8_t)
        if (block->q_cnt > 0) {
            memcpy(&my_data[idx], block->q_data_type_table, q_type_size);
            idx += q_type_size;
            memcpy(&my_data[idx], block->q_data, q_data_size);
            idx += q_data_size;
        }     
        // 9.Global refs
        memcpy(&my_data[idx], &block->global_reference_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);
        memcpy(&my_data[idx], &block->in_global_used, sizeof(uint16_t));
        idx += sizeof(uint16_t);
    }

    chr_msg_buffer_add(output, my_data, idx);
    
    return res;
}   
    



emu_result_t emu_debug_output_add(chr_msg_buffer_t * msg){
    emu_result_t res = {.code = EMU_OK};
    if (NULL==msg){
        ESP_LOGW(TAG, "No buffer provided to assign");
        res.code = EMU_ERR_NULL_PTR;
        res.warning = true;
        return res;   
    }
    output = msg;
    return res;
}