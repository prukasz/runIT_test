#include <stdint.h>
#include "emu_logging.h"


typedef struct{
    mem_access_t *instance;
    //fast path//
    uint16_t data_size_in_bytes;
    uint16_t self_index;
    uint8_t context   : 3;  /*Context that isnstance shall belong to*/
    uint8_t type      : 4;  /*mem_types_t type*/
    uint8_t updated   : 1;  /*Updated flag can be used for block output variables*/
}emu_subscribe_buff_t;



emu_result_t emu_subscribe_init(const uint16_t count);
emu_result_t emu_subscribe_register(const mem_access_t* what, const uint16_t index);
emu_result_t emu_subscribe_process();
emu_result_t emu_subscribe_reset();
emu_result_t emu_subscribe_send();

emu_result_t emu_parse_subscription_init(const uint8_t *data, const uint16_t data_len, void *nothing);

emu_result_t emu_parse_subscription_add(const uint8_t *data, const uint16_t data_len, void *nothing);
