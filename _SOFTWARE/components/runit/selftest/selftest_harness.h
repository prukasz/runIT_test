#pragma once
#include <esp_log.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_error.h"
#include "vm_obj.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

#define TAG "vm_selftest"

#define OWNER OWNER_VM_BASE

typedef void (*selftest_fn_t)(void);

typedef struct {
  const char* id;
  const char* name;
  selftest_fn_t run;
  bool enabled;
} selftest_stage_t;

// Assertion reporting
void selftest_ck(const char* what, bool ok);
#define ck(what, ok) selftest_ck(what, ok)

int selftest_get_pass(void);
int selftest_get_fail(void);
void selftest_reset_counts(void);

// Common arena allocator reset for direct tests
#define DIRECT_POOL 4096
void direct_arena_reset(void);
uint16_t selftest_next_acc_id(void);

// Common object helpers
vm_obj_head_t hd(vm_obj_t_e type, uint16_t items);
vm_obj_h mk(uint16_t id, vm_obj_t_e type, uint16_t items, const char* name, bool mutable_);
uint32_t kf(float f);
bool near_f(float a, float b);
