#pragma once
#include "selftest_harness.h"
#include "sys_interface.h"
#include "vm_loader.h"

#define OBJ_MSG 0
#define OBJ_TEMP 1
#define OBJ_HUM 2

void f_begin(uint8_t class_header, uint8_t packet);
void f_u8(uint8_t v);
void f_u16(uint16_t v);
void f_u32(uint32_t v);
void f_blob(const void* p, size_t n);
void f_str(const char* s);
void f_f32(float v);
err_h f_send(void);

err_h upload_open_sec(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint16_t sec_cnt, uint32_t total);
err_h upload_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint32_t total);
void add_obj_record_raw(uint16_t id, uint16_t payload_size, uint8_t type, uint8_t flags, const char* name);
void add_obj_record(uint16_t id, uint16_t item_count, uint8_t type, uint8_t flags, const char* name);

extern uint8_t s_idx[64];
extern uint8_t s_idx_len;

void ix_begin(void);
void ix_literal(uint32_t v);
void ix_ref(uint16_t acc_id);
void ix_name(const char* n);
void acc_record(uint16_t acc_id, uint16_t root_id, uint8_t idx_count);
