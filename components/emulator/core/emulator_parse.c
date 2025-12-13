#include "emulator_parse.h"
#include "emulator_blocks.h"
#include "emulator_body.h"
#include "emulator_types.h"
#include "emulator_errors.h"
#include "utils_block_in_q_access.h"
#include "utils_global_access.h"
#include "block_math.h"

static const char *TAG = "EMU_PARSE";


extern chr_msg_buffer_t *source;
extern block_handle_t **emu_block_struct_execution_list;
extern uint16_t emu_block_total_cnt;
emu_err_t emu_parse_manager(parse_cmd_t cmd){
    static struct {
        bool can_create_variables;
        bool can_fill_variables;
        bool can_create_blocks;
        bool can_fill_blocks;
        bool can_create_blocks_list;
        bool finished;
        bool can_run_code;
    } flags = {0};

    static struct {
        bool is_create_variables_done;
        bool is_fill_variables_done;
        bool is_create_blocks_done; 
        bool is_fill_blocks_done;
        bool is_parse_finised;
        bool is_create_blocks_list_done;
    } status = {0};

    switch (cmd)
    {
    case PARSE_RESTART:
        //clear flags
        memset(&flags, 0, sizeof(flags));
        memset(&status, 0, sizeof(status));
        flags.can_create_variables = true;
        break;
    case PARSE_CREATE_VARIABLES:
        if(flags.can_create_variables  && !status.is_create_variables_done){
            emu_err_t err= emu_parse_variables(source, &mem);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While parsing and creating variables error: %d", err);
                return err;
            }
            flags.can_fill_variables = true; 
            flags.can_create_variables = false;
            status.is_create_variables_done = true;
            ESP_LOGI(TAG, "Succesfuly parsed variables");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't create variables right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_FILL_VARIABLES:
        if(flags.can_fill_variables && status.is_create_variables_done){
            emu_err_t err= emu_parse_variables_into(source, &mem);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While parsing into variables error: %d", err);
                return err;
            }
            if(status.is_fill_variables_done){
                ESP_LOGW(TAG, "Parsing already done reparsing now, some values can be overwritten");
                return EMU_OK;
            }
            flags.can_fill_variables = true;
            status.is_fill_variables_done = true;
            flags.can_create_blocks_list = true;
            ESP_LOGI(TAG, "Succesfuly filled variables");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't fill variables rigth now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_CREATE_BLOCKS_LIST:
        if (flags.can_create_blocks_list){
            emu_err_t err = emu_parse_total_block_cnt(source);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While alocating blocks space error: %d", err);
                return err;
            }
            flags.can_create_blocks = true;
            flags.can_create_blocks_list = false;
            status.is_create_blocks_list_done = true;
        }else{
            ESP_LOGE(TAG, "Can't create blocks list, first create varaibles");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;

    case PARSE_CREATE_BLOCKS:
        if(flags.can_create_blocks && status.is_create_blocks_list_done){
            emu_err_t err = emu_parse_block(source, emu_block_struct_execution_list, emu_block_total_cnt);
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While creating blocks error: %d", err);
                return err;
            }
            flags.can_create_blocks = false;
            flags.can_fill_blocks = true;
            status.is_create_blocks_done = true;
            ESP_LOGI(TAG, "Succesfuly created blocks");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't create blocks right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_FILL_BLOCKS:
        if(flags.can_fill_blocks && status.is_create_blocks_done){
            emu_err_t err=emu_parse_fill_block_data();
            if(EMU_OK!=err){
                ESP_LOGE(TAG, "While filling blocks error: %d", err);
                return err;
            }
            flags.can_fill_blocks = false;
            status.is_fill_blocks_done = true;
            flags.can_run_code = true;
            ESP_LOGI(TAG, "Succesfuly filled created blocks");
            return EMU_OK;
        }else{
            ESP_LOGE(TAG, "Can't fill blocks right now");
            return EMU_ERR_PARSE_INVALID_REQUEST;
        }
        break;
    case PARSE_CHEKC_CAN_RUN:
        if(flags.can_run_code && status.is_fill_blocks_done){
            return EMU_OK;
        }else{
            return EMU_ERR_DENY;
        }
        break;
    
    case PARSE_IS_CREATE_BLOCKS_DONE:
        if(status.is_create_blocks_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;

    case PARSE_IS_CREATE_VARIABLES_DONE:
        if(status.is_create_variables_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;
    
    case PARSE_IS_FILL_VARIABLES_DONE:
        if(status.is_fill_variables_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;
    
    case PARSE_IS_FILL_BLOCKS_DONE:
        if(status.is_fill_blocks_done){
            return EMU_OK;
        }else{return EMU_ERR_DENY;}
        break;    

    default:
        break;
    }

    return EMU_ERR_UNLIKELY;
}

emu_err_t emu_parse_fill_block_data(){
    for(uint16_t i = 0; i<emu_block_total_cnt; i++){
        block_handle_t * block = emu_block_struct_execution_list[i];
        //if block already created and has no data
        if(block&&!block->extras){
            switch(block->block_type)
            {
                case BLOCK_COMPUTE:
                LOG_I(TAG, "Now will fill block data for block BLOCK_COMPUTE");
                if(emu_parse_math_blocks(source)!=EMU_OK){
                    ESP_LOGE(TAG, "While parsing math block data for block %d", i);
                    return EMU_ERR_NO_MEM;
                }
                break;

                default:
                break;
            }
        }
    }
    return EMU_OK;
}