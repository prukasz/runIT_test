#include "vm_override.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "utils.h"
#include "vm_obj.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_store.h"

#define OWNER OWNER_VM_EXEC

static const char* TAG = "vm_override";

// Static ring buffer initialized at startup via constructor macro
R_RINGBUFFER_DEFINE(s_override_rb, VM_OVERRIDE_BUF_SIZE, RINGBUF_TYPE_NOSPLIT);

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
  if (obj->head.f.usr_protected) {
    SE_RET_ERR(ERR_VM_OBJ_USR_PROTECTED, .obj = obj);
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

  if (unlikely(!s_override_rb)) {
    SE_RET_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = len, .remaining = 0);
  }

  size_t rec_size = sizeof(vm_override_record_t) + len;
  void* item_mem = NULL;
  // Non-blocking acquire from caller context (decoder task)
  if (xRingbufferSendAcquire(s_override_rb, &item_mem, rec_size, 0) != pdTRUE || !item_mem) {
    SE_RET_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = rec_size, .remaining = 0);
  }

  vm_override_record_t* rec = (vm_override_record_t*)item_mem;
  rec->id = id;
  rec->start_idx = start_idx;
  rec->len = len;
  if (len > 0) {
    memcpy(rec->data, data, len);
  }

  if (xRingbufferSendComplete(s_override_rb, item_mem) != pdTRUE) {
    SE_RET_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = rec_size, .remaining = 0);
  }

  return NULL;
}

void vm_override_drain(void) {
  if (unlikely(!s_override_rb)) return;

  size_t item_size = 0;
  void* item = NULL;

  while ((item = xRingbufferReceive(s_override_rb, &item_size, 0)) != NULL) {
    if (item_size >= sizeof(vm_override_record_t)) {
      const vm_override_record_t* rec = (const vm_override_record_t*)item;
      vm_obj_h obj = vm_obj_get_by_id(rec->id);
      if (obj && obj->head.f.mutable && !obj->head.f.usr_protected) {
        uint8_t w = vm_obj_get_type_size(obj);
        if (w > 0 && rec->len > 0) {
          memcpy(obj->payload + (size_t)rec->start_idx * w, rec->data, rec->len);
          obj->head.f.upd = 1;
          ESP_LOGI(TAG, "override applied: id %u [%u..%u], %u bytes",
                   rec->id, rec->start_idx, rec->start_idx + (rec->len / w) - 1, rec->len);
        }
      }
    }
    vRingbufferReturnItem(s_override_rb, item);
  }
}

void vm_override_reset(void) {
  if (unlikely(!s_override_rb)) return;

  // Drain and return all items in the ring buffer
  size_t item_size = 0;
  void* item = NULL;
  while ((item = xRingbufferReceive(s_override_rb, &item_size, 0)) != NULL) {
    vRingbufferReturnItem(s_override_rb, item);
  }
}