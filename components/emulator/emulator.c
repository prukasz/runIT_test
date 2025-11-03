#include "emulator_loop.h"
#include "emulator_variables.h"
#include "gatt_buff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "emulator.h"
#include "string.h"


static chr_msg_buffer_t *source;
static const char * TAG = "EMULATOR TASK";
QueueHandle_t emu_task_q;

static bool is_code_start;
static bool is_code_end;
static bool is_blocks_start;

emu_err_t code_set_start();
emu_err_t code_set_blocks();
emu_err_t code_set_end();
void code_set_reset();

SemaphoreHandle_t emulator_start;
emu_mem_t mem;
uint8_t emu_mem_size[11];


emu_err_t emulator_source_assign(chr_msg_buffer_t * msg){
    if (msg == NULL){
        return EMU_ERR_INVALID_ARG;
    }
    source = msg;
    return EMU_OK;
}

//emu_err_t emulator_source_analyze(){} 

/**
 * @brief Parses variable definitions from the BLE source buffer and creates memory holders.
 *
 * This function implements the parsing logic for your variable definition protocol:
 * 1. Finds `FFFF` (start) + `FF00` (vars).
 * 2. Parses 11 bytes for simple 1D array counts.
 * 3. Finds `FF01`, `FF02`, `FF03` packets for MD array definitions.
 * 4. Creates both simple and MD data holders.
 */
emu_err_t emulator_process_variables_from_source() {
    uint8_t *data;
    uint16_t len;
    int start_index = -1;

    // --- Find the start of the variable definition sequence ---
    for (int i = 0; i < chr_msg_buffer_size(source); ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len == 2 && ((data[0] << 8) | data[1]) == ORD_START_BYTES) {
            // Found FFFF, check if the next packet is FF00
            if ((i + 1) < chr_msg_buffer_size(source)) {
                chr_msg_buffer_get(source, i + 1, &data, &len);
                if (len == 12 && ((data[0] << 8) | data[1]) == ORD_VARIABLES_BYTES) {
                    ESP_LOGI(TAG, "Found variable definition start at buffer index %d", i);
                    start_index = i + 1;
                    break;
                }
            }
        }
    }

    if (start_index == -1) {
        ESP_LOGE(TAG, "Variable definition start sequence (FFFF -> FF00) not found.");
        return EMU_ERR_INVALID_DATA;
    }

    // --- 1. Parse and create simple 1D arrays ---
    chr_msg_buffer_get(source, start_index, &data, &len);
    memcpy(emu_mem_size, &data[2], 10); // Copy the 10 size bytes
    ESP_LOGI(TAG, "Creating simple 1D data holder.");
    for(int i=0; i<11; ++i) ESP_LOGD(TAG, "  type %d: count %d", i, emu_mem_size[i]);
    emulator_dataholder_create(&mem, emu_mem_size);

    // --- 2. Parse and create multi-dimensional (MD) arrays ---
    size_t md_desc_capacity = 4; // Initial capacity
    size_t md_desc_count = 0;
    emu_md_variable_desc_t *md_descriptors = malloc(md_desc_capacity * sizeof(emu_md_variable_desc_t));
    if (!md_descriptors) return EMU_ERR_NO_MEMORY;

    // Iterate through the rest of the buffer to find MD definitions
    for (int i = start_index + 1; i < chr_msg_buffer_size(source); ++i) {
        chr_msg_buffer_get(source, i, &data, &len);
        if (len < 3) continue; // Not a valid MD packet

        uint16_t md_header = (data[0] << 8) | data[1];
        uint8_t *payload = &data[2];
        uint16_t payload_len = len - 2;
        uint8_t num_dims = 0;
        uint8_t record_size = 0;

        if (md_header == 0xFF01) { num_dims = 1; record_size = 2; } // type, dim1
        else if (md_header == 0xFF02) { num_dims = 2; record_size = 3; } // type, dim1, dim2
        else if (md_header == 0xFF03) { num_dims = 3; record_size = 4; } // type, dim1, dim2, dim3
        else continue; // Not an MD definition packet

        if (payload_len % record_size != 0) {
            ESP_LOGW(TAG, "Invalid payload length for MD packet 0x%04X", md_header);
            continue;
        }

        int num_records = payload_len / record_size;
        ESP_LOGI(TAG, "Found %d-D variable packet with %d records.", num_dims, num_records);

        // Resize descriptor array if needed
        if (md_desc_count + num_records > md_desc_capacity) {
            md_desc_capacity = md_desc_count + num_records + 4;
            emu_md_variable_desc_t *new_ptr = realloc(md_descriptors, md_desc_capacity * sizeof(emu_md_variable_desc_t));
            if (!new_ptr) { free(md_descriptors); return EMU_ERR_NO_MEMORY; }
            md_descriptors = new_ptr;
        }

        // Parse each record in the payload
        for (int j = 0; j < num_records; ++j) {
            emu_md_variable_desc_t *desc = &md_descriptors[md_desc_count++];
            desc->type = (data_types_t)payload[0];
            desc->num_dims = num_dims;
            desc->dims[0] = (num_dims >= 1) ? payload[1] : 0;
            desc->dims[1] = (num_dims >= 2) ? payload[2] : 0;
            desc->dims[2] = (num_dims >= 3) ? payload[3] : 0;
            payload += record_size;
        }
    }

    if (md_desc_count > 0) {
        ESP_LOGI(TAG, "Creating MD data holder with %d variables.", md_desc_count);
        emulator_md_dataholder_create(&mem, md_descriptors, md_desc_count);
    } else {
        ESP_LOGI(TAG, "No MD variables defined.");
    }

    free(md_descriptors);
    return EMU_OK;
}

//emu_err_t emulator_source_get_config(){}  //get config data from source

emu_err_t loop_start_execution(){
    loop_start();
    return EMU_OK;
}  //start execution

emu_err_t loop_stop_execution(){
    loop_stop();
    return EMU_OK;
}   //stop and preserve state

//emu_err_t emulator_halt_execution(){}   //emergency stop

//emu_err_t emulator_end_execution(){}    //end execution (finish loop)

//emu_err_t emulator_debug_varialbles(){}  //dump values

//emu_err_t emulator_debug_code(){}   //dump code execution state

//emu_err_t emulator_run_with_remote(){}   //allows remote interaction

void loop_task(void* params){
    //form here will be executed all logic 
    while(1){
        if(pdTRUE == xSemaphoreTake(emulator_start, portMAX_DELAY)) {
            ESP_LOGI(TAG, "--- Loop Task Tick ---");

            // Simple test: read a value, increment it, and write it back.
            uint8_t data =  MEM_GET(DATA_UI8, 0);
            ESP_LOGI(TAG, "Read simple var DATA_UI8[0]: %d", data);
            data++;
            MEM_SET(DATA_UI8, 0, &data);
            ESP_LOGI(TAG, "----------------------");
            }
        taskYIELD();
    }
}


block_handle_t *block_init(block_define_t *block_define){
    //init block
    block_handle_t *block = (block_handle_t*)calloc(1,sizeof(block_handle_t));
    block->id = block_define->id; 
    block->type = block_define->type;
    block->in_cnt = block_define->in_cnt;
    block->in_data_type_table = (data_types_t*)calloc((block->in_cnt),sizeof(data_types_t));
    memcpy(block->in_data_type_table, block_define->in_data_type_table, block->in_cnt*sizeof(data_types_t));
    block->q_cnt = block_define->q_cnt;
    block->q_nodes_cnt = block_define->q_nodes_cnt;
    block->q_node_ids = (uint16_t*)calloc(block->q_nodes_cnt, sizeof(uint16_t));
    memcpy(block->q_node_ids, block_define->q_node_ids, block->q_nodes_cnt*sizeof(uint16_t));
    block->q_data_type_table = (data_types_t*)calloc((block->q_cnt),sizeof(data_types_t));
    free(block_define);
    
    
    block->in_data_offsets=(size_t*)calloc(block->in_cnt,sizeof(size_t));
    size_t size_to_allocate = 0;
    //allocating space for left pointers
    for(int i = 0; i<block->in_cnt; i++)
    {
        block->in_data_offsets[i] = size_to_allocate;
        size_to_allocate = size_to_allocate + data_size(block->in_data_type_table[i]);
    }

    block->in_data = (void*)calloc(1,size_to_allocate);

    block->q_data_offsets=(size_t*)calloc(block->q_cnt,sizeof(size_t));
    size_to_allocate = 0;
    for(int i = 0; i<block->q_cnt; i++)
    {
        block->q_data_offsets[i] = size_to_allocate;
        size_to_allocate = size_to_allocate + data_size(block->q_data_type_table[i]);
    }

    block->q_data = (void*)calloc(1,size_to_allocate);

    return block;
}

void emu(void* params){
    ESP_LOGI(TAG, "emu task active");
    emu_task_q  = xQueueCreate(3, sizeof(emu_order_code_t));
    static emu_order_code_t orders;
    while(1){
        if (pdTRUE == xQueueReceive(emu_task_q, &orders, portMAX_DELAY)){
            ESP_LOGI(TAG, "execute order 0x%04X" , orders);
            switch (orders){
            case ORD_PROCESS_VARIABLES:
                emulator_process_variables_from_source();
                break;

            case ORD_PROCESS_CODE:
                // Handle processing code
                break;

            case ORD_CHECK_CODE:
                // Handle checking completeness
                break;

            case ORD_START_BLOCKS:
                // Handle starting blocks
                code_set_blocks();
                break;

            case ORD_EMU_LOOP_RUN:
                loop_start();
                break;
            case ORD_EMU_LOOP_INIT:
                loop_init();
            break;

            case ORD_EMU_LOOP_STOP:
                loop_stop();
                break;

            case ORD_RESET_TRANSMISSION:
                // Handle reset
                code_set_reset();
                chr_msg_buffer_clear(source);
                break;

            case ORD_START_BYTES:
                // Handle start bytes
                code_set_start();
                break;

            case ORD_STOP_BYTES:
                code_set_end();
                break;

            default:
                // Unknown order
                break;
            }// end case
        }//end if
    } //end while
};



//flags for transmission keypoints
emu_err_t code_set_start(){
    if (is_code_start == true){
        ESP_LOGE(TAG, "code transmission already started");
        return EMU_ERR_CMD_START;
    }
    is_code_start = true;
    ESP_LOGI(TAG, "code transmission started");
    return EMU_OK;
}
emu_err_t code_set_blocks(){
    if (is_blocks_start == true){
        ESP_LOGE(TAG, "blocks transmission already started");
        return EMU_ERR_CMD_START;
    }
    is_blocks_start = true;
    ESP_LOGI(TAG, "blocks transmission started");
    return EMU_OK;
}
emu_err_t code_set_end(){
    if (is_code_end == true){
        ESP_LOGE(TAG, "code already stopped");
        return EMU_ERR_CMD_START;
    }
    is_code_end = true;
    ESP_LOGI(TAG, "code end reached");
    return EMU_OK;
}

void code_set_reset(){
    is_code_start   = false;
    is_blocks_start = false;
    is_code_end     = false;
    return;
}
