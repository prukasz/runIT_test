size_t idx = 0;

    for (uint16_t i = 0; i < total_block_cnt; i++) {
        block_handle_t *block = block_struct_list[i];
        
        // 1. Block ID (uint16_t)
        memcpy(&my_data[idx], &block->block_idx, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 2. Block Type (uint8_t)
        uint8_t b_type = (uint8_t)block->block_type;
        memcpy(&my_data[idx], &b_type, sizeof(uint8_t));
        idx += sizeof(uint8_t);

        // --- INPUTS ---

        // 3. In Count (uint8_t)
        memcpy(&my_data[idx], &block->in_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);

        // 4. In Used Mask (uint16_t)
        memcpy(&my_data[idx], &block->in_used, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 5. In Set Mask (uint16_t)
        memcpy(&my_data[idx], &block->in_set, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 6. In Types Table (Array of uint8_t)
        if (block->in_cnt > 0) {
            memcpy(&my_data[idx], block->in_data_type_table, block->in_cnt);
            idx += block->in_cnt;

            // 7. In Data (Raw Bytes)
            // Calculate size dynamically based on types to be safe
            size_t in_size = 0;
            for(uint8_t k = 0; k < block->in_cnt; k++) {
                in_size += data_size(block->in_data_type_table[k]);
            }
            
            if (block->in_data && in_size > 0) {
                memcpy(&my_data[idx], block->in_data, in_size);
                idx += in_size;
            }
        }

        // --- OUTPUTS ---

        // 8. Q Count (uint8_t)
        memcpy(&my_data[idx], &block->q_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);

        // 9. Q Types Table (Array of uint8_t)
        if (block->q_cnt > 0) {
            memcpy(&my_data[idx], block->q_data_type_table, block->q_cnt);
            idx += block->q_cnt;

            // 10. Q Data (Raw Bytes)
            size_t q_size = 0;
            for(uint8_t k = 0; k < block->q_cnt; k++) {
                q_size += data_size(block->q_data_type_table[k]);
            }

            if (block->q_data && q_size > 0) {
                memcpy(&my_data[idx], block->q_data, q_size);
                idx += q_size;
            }
        }

        // --- GLOBALS ---

        // 11. In Global Used Mask (uint16_t)
        memcpy(&my_data[idx], &block->in_global_used, sizeof(uint16_t));
        idx += sizeof(uint16_t);

        // 12. Global Reference Count (uint8_t)
        memcpy(&my_data[idx], &block->global_reference_cnt, sizeof(uint8_t));
        idx += sizeof(uint8_t);
    }