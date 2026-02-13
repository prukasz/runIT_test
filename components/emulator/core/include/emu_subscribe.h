#include <stdint.h>
#include "emu_logging.h"


emu_result_t emu_subscribe_parse_init(const uint8_t *packet_data, const uint16_t packet_len, void* custom);
emu_result_t emu_subscribe_parse_register(const uint8_t *packet_data, const uint16_t packet_len, void* custom);
emu_result_t emu_subscribe_process();
emu_result_t emu_subscribe_reset();
emu_result_t emu_subscribe_send();

emu_result_t emu_parse_subscription_init(const uint8_t *data, const uint16_t el_cnt, void *nothing);

emu_result_t emu_parse_subscription_add(const uint8_t *packet_data, const uint16_t packet_len, void* custom);
