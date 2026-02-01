#pragma once
#include "emu_types.h"
#include <string.h>
#include "mem_types.h"


static inline bool parse_check_header(const uint8_t *data, size_t data_len, const uint8_t header){
    if (data[0] != header) return false;
    return true;
}


static inline uint16_t parse_get_u16(const uint8_t *data, uint16_t offset) {
    uint16_t tmp;
    memcpy(&tmp, data + offset, sizeof(uint16_t));
    return tmp;
}

static inline int16_t parse_get_i16(const uint8_t *data, uint16_t offset) {
    int16_t tmp;
    memcpy(&tmp, data + offset, sizeof(int16_t));
    return tmp;
}

static inline uint32_t parse_get_u32(const uint8_t *data, uint16_t offset) {
    uint32_t tmp;
    memcpy(&tmp, data + offset, sizeof(uint32_t));
    return tmp;
}

static inline int32_t parse_get_i32(const uint8_t *data, uint16_t offset) {
    int32_t tmp;
    memcpy(&tmp, data + offset, sizeof(int32_t));
    return tmp;
}

static inline float parse_get_f(const uint8_t *data, uint16_t offset) {
    float tmp;
    memcpy(&tmp, data + offset, sizeof(float));
    return tmp;
}

static inline double parse_get_d(const uint8_t *data, uint16_t offset) {
    double tmp;
    memcpy(&tmp, data + offset, sizeof(double));
    return tmp;
}



