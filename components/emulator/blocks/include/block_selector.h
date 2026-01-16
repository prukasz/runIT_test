#pragma once
#include <stdint.h>

typedef struct{
    uint8_t ref_cnt;
    void** ref_table;
}