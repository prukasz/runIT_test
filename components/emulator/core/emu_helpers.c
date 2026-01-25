#include "emu_helpers.h"
#include "string.h"


bool parse_check_u16(uint8_t *data, uint16_t expected_val) {
    uint16_t val_in_packet;
    memcpy(&val_in_packet, data, sizeof(uint16_t));
    return (val_in_packet == expected_val);
}

