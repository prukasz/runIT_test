#include "vm_override.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "vm_obj.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_store.h"

#define OWNER OWNER_VM_EXEC

static const char* TAG = "vm_override";

static portMUX_TYPE s_override_mux = portMUX_INITIALIZER_UNLOCKED;
static vm_override_record_t s_override_ring[VM_OVERRIDE_QUEUE_DEPTH];
static uint16_t s_head = 0;
static uint16_t s_tail = 0;
static uint16_t s_count = 0;

err_h vm_override_post(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len) {
  if (!data && len > 0) {
    SE_RET_ERR(ERR_NULL_PTR, 0);
  }
  vm_obj_h obj = vm_obj_get_by_id(id);
  if (!obj) {
    SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
  }
  if (!obj->head.f.mutable) {
    SE_RET_ERR(ERR_VM_OBJ_NOT_MUTABLE, .obj = obj);
  }
  if (len > VM_OVERRIDE_MAX_DATA) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = vm_obj_get_items_cnt(obj));
  }
  uint8_t w = vm_obj_get_type_size(obj);
  uint16_t items = vm_obj_get_items_cnt(obj);
  if (w == 0 || (len % w) != 0) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
  }
  uint16_t n = len / w;
  if ((uint32_t)start_idx + n > items) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
  }

  portENTER_CRITICAL(&s_override_mux);
  if (s_count >= VM_OVERRIDE_QUEUE_DEPTH) {
    portEXIT_CRITICAL(&s_override_mux);
    SE_RET_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = len, .remaining = 0);
  }
  vm_override_record_t* rec = &s_override_ring[s_head];
  rec->id = id;
  rec->start_idx = start_idx;
  rec->len = len;
  if (len > 0) {
    memcpy(rec->data, data, len);
  }
  s_head = (s_head + 1) % VM_OVERRIDE_QUEUE_DEPTH;
  s_count++;
  portEXIT_CRITICAL(&s_override_mux);

  return NULL;
}

void vm_override_drain(void) {
  vm_override_record_t batch[VM_OVERRIDE_QUEUE_DEPTH];
  uint16_t batch_count = 0;

  portENTER_CRITICAL(&s_override_mux);
  while (s_count > 0 && batch_count < VM_OVERRIDE_QUEUE_DEPTH) {
    batch[batch_count++] = s_override_ring[s_tail];
    s_tail = (s_tail + 1) % VM_OVERRIDE_QUEUE_DEPTH;
    s_count--;
  }
  portEXIT_CRITICAL(&s_override_mux);

  for (uint16_t i = 0; i < batch_count; i++) {
    const vm_override_record_t* rec = &batch[i];
    vm_obj_h obj = vm_obj_get_by_id(rec->id);
    if (!obj || !obj->head.f.mutable) {
      continue;
    }
    uint8_t w = vm_obj_get_type_size(obj);
    if (w == 0) continue;
    memcpy(obj->payload + (size_t)rec->start_idx * w, rec->data, rec->len);
    obj->head.f.upd = 1;
    ESP_LOGI(TAG, "override applied: id %u [%u..%u], %u bytes",
             rec->id, rec->start_idx, rec->start_idx + (rec->len / w) - 1, rec->len);
  }
}

void vm_override_reset(void) {
  portENTER_CRITICAL(&s_override_mux);
  s_head = 0;
  s_tail = 0;
  s_count = 0;
  portEXIT_CRITICAL(&s_override_mux);
}

uint16_t vm_override_pending_count(void) {
  portENTER_CRITICAL(&s_override_mux);
  uint16_t c = s_count;
  portEXIT_CRITICAL(&s_override_mux);
  return c;
}
