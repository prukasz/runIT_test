#include "block_timer.h"
#include "emu_loop.h"
#include "emu_logging.h"
#include "emu_variables_acces.h" 
#include "emu_blocks.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = __FILE_NAME__;


#define BLOCK_TIMER_IN_EN   0
#define BLOCK_TIMER_IN_PT   1
#define BLOCK_TIMER_IN_RST  2

#define BLOCK_TIMER_OUT_Q   0
#define BLOCK_TIMER_OUT_ET  1

/* Timer Types */
typedef enum {
    TIMER_TYPE_TON = 0x01, // On-Delay
    TIMER_TYPE_TOF = 0x02, // Off-Delay
    TIMER_TYPE_TP  = 0x03,  // Pulse
    TIMER_TYPE_TON_INV = 0x04,
    TIMER_TYPE_TOF_INV = 0x05,
    TIMER_TYPE_TP_INV = 0x06
} block_timer_type_t;


typedef struct {
    block_timer_type_t type;
    uint64_t start_time;
    uint32_t default_pt; 
    uint32_t delta_time;  
    bool q_out;            
    bool prev_in;                  
    bool counting;         
} block_timer_t;

/* Inputs/Outputs Indices */
#define BLOCK_TIMER_IN_EN    0
#define BLOCK_TIMER_IN_PT    1
#define BLOCK_TIMER_IN_RST   2

#define BLOCK_TIMER_OUT_Q    0
#define BLOCK_TIMER_OUT_ET   1  //elapsed time 

/* ========================================================================= */
/* LOGIKA WYKONAWCZA                                                         */
/* ========================================================================= */

#undef OWNER
#define OWNER EMU_OWNER_block_timer
emu_result_t block_timer(block_handle_t block) {

    uint64_t now_ms = emu_loop_get_time();
    block_timer_t* data = (block_timer_t*)block->custom_data;
    emu_result_t res = EMU_RESULT_OK();

    bool IN = block_check_in_true(block, BLOCK_TIMER_IN_EN);

    // RST (Reset)
    bool RST = false;
    if(block_in_updated(block, BLOCK_TIMER_IN_RST)){MEM_GET(&RST, block->inputs[BLOCK_TIMER_IN_RST]);}

    uint32_t PT = data->default_pt; 
    if(block_in_updated(block, BLOCK_TIMER_IN_PT)){MEM_GET(&PT, block->inputs[BLOCK_TIMER_IN_PT]);}

    block_timer_type_t type = data->type;

    LOG_I(TAG, "Type: %d, PT: %ld,  delta: %ld, now %lld", type, PT, data->delta_time, now_ms);
    if (RST) {
        data->delta_time = 0;
        data->q_out = (type == TIMER_TYPE_TOF); // TOF w resecie ma Q=1 (zazwyczaj)
        data->counting = false;
        data->start_time = 0; 
    } 
    else {
        switch (type) {
            case TIMER_TYPE_TON:
                if (IN) {
                    if (!data->counting) {
                        data->start_time = now_ms;
                        data->counting = true;
                        data->q_out = false;
                    } else {
                        data->delta_time = now_ms - data->start_time;
                        if (data->delta_time >= PT) {
                            data->q_out = true;
                            data->delta_time = PT; // Limituj ET do PT
                        }
                    }
                } else { 
                    data->delta_time = 0;
                    data->q_out = false;
                    data->counting = false;
                }
                break;

            case TIMER_TYPE_TOF:
                if (IN) {
                    if (!data->counting && !data->prev_in) {
                        data->start_time = now_ms;
                        data->counting = true;
                    }
                    
                    if (data->counting) {
                        data->delta_time = now_ms - data->start_time;
                        if (data->delta_time >= PT) {
                            data->q_out = false;
                            data->counting = false;
                            data->delta_time = PT;
                        } else {
                            data->q_out = true;
                        }
                    } else {
                        data->q_out = false;
                        data->delta_time = 0;
                    }
                } else { 
                    data->delta_time = 0;
                    data->q_out = true;
                    data->counting = false;
                }
                break;

            case TIMER_TYPE_TP:
                if (IN && !data->prev_in && !data->counting) {
                    data->counting = true;
                    data->start_time = now_ms;
                    data->q_out = true;
                }

                if (data->counting) {
                    data->delta_time = now_ms - data->start_time;
                    if (data->delta_time >= PT) {
                        data->q_out = false;
                        data->counting = false;
                        data->delta_time = PT;
                    } else {
                        data->q_out = true;
                    }
                }
                break;

            default:
                break;
        }
    }

    data->prev_in = IN;
    
    mem_var_t v_en = { .type = MEM_B, .data.val.b = data->q_out};
    LOG_I(TAG, "is out active %d", data->q_out);
    res = block_set_output(block, v_en, 0);
    if (unlikely(res.code != EMU_OK)) {
        RET_ED(res.code, block->cfg.block_idx, 0, "Output acces error %s", EMU_ERR_TO_STR(res.code));
    }
    
    // Ustawienie wyjścia ET (czasu) - brakowało tego w twoim kodzie, ale zgodnie z logiką timera powinno być
    // Zakładam, że chcesz to dodać, skoro liczysz delta_time.
    // Jeśli nie chcesz zmieniać logiki, usuń poniższy blok.
    mem_var_t v_et = { .type = MEM_F, .data.val.f = (float)data->delta_time };
    res = block_set_output(block, v_et, BLOCK_TIMER_OUT_ET);
    if (unlikely(res.code != EMU_OK)) {
         RET_ED(res.code, block->cfg.block_idx, 0, "Output ET error %s", EMU_ERR_TO_STR(res.code));
    }

    return EMU_RESULT_OK();
}


#undef OWNER
#define OWNER EMU_OWNER_block_timer_parse
emu_result_t block_timer_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block_ptr) {
    block_handle_t block = (block_handle_t)block_ptr;
    if (!block) RET_E(EMU_ERR_NULL_PTR, "NULL block");
    
    // Packet: [packet_id:u8][data...]
    if (packet_len < 1) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");
    
    uint8_t packet_id = packet_data[0];
    const uint8_t *payload = &packet_data[1];
    uint16_t payload_len = packet_len - 1;
    
    // Allocate custom data if not exists
    if (!block->custom_data) {
        block->custom_data = calloc(1, sizeof(block_timer_t));
        if (!block->custom_data) RET_ED(EMU_ERR_NO_MEM, block->cfg.block_idx, 0, "Custom data alloc failed");
    }
    
    block_timer_t *handle = (block_timer_t*)block->custom_data;
    
    // PKT_CFG (0x01): [timer_type:u8][default_pt:u32]
    if (packet_id == BLOCK_PKT_CFG) {
        if (payload_len < 5) RET_ED(EMU_ERR_PACKET_INCOMPLETE, block->cfg.block_idx, 0, "Config payload too short");
        
        handle->type = (block_timer_type_t)payload[0];
        memcpy(&handle->default_pt, &payload[1], sizeof(uint32_t));
        
        LOG_I(TAG, "Timer Config Loaded -> Type=%d, DefaultPT=%lu ms", handle->type, (unsigned long)handle->default_pt);
    }
    
    return EMU_RESULT_OK();
}

void block_timer_free(block_handle_t block) {
    if (block->custom_data) {
        free(block->custom_data);
        block->custom_data = NULL;
        LOG_D(TAG, "Cleared timer data");
    }
}

#undef OWNER
#define OWNER EMU_OWNER_block_timer_verify
emu_result_t block_timer_verify(block_handle_t block) {
    if (!block->custom_data) {RET_ED(EMU_ERR_NULL_PTR, block->cfg.block_idx, 0, "Custom Data is NULL");}

    block_timer_t *data = (block_timer_t*)block->custom_data;

    if (data->type > TIMER_TYPE_TP_INV) {RET_ED(EMU_ERR_BLOCK_INVALID_PARAM, block->cfg.block_idx, 0, "Invalid Timer Type: %d", data->type);}

    return EMU_RESULT_OK();
}
