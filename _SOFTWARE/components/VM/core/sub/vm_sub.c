#include "vm_sub.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>
#include "vm_exec.h"
#include "vm_obj_access.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

#define OWNER OWNER_VM_BASE
#define TAG "vm_sub"

#define MAX_EMITTED_PER_PASS 256

static uint16_t s_subscribed_ids[VM_SUB_MAX_SUBSCRIBERS];
static uint16_t s_sub_count = 0;
static vm_sub_sender_fn s_sender = NULL;

static uint16_t s_emitted[MAX_EMITTED_PER_PASS];
static uint16_t s_emitted_count = 0;

typedef struct {
  uint8_t buf[VM_SUB_MAX_FRAME_LEN];
  size_t len;
  uint8_t count;
} sub_frame_t;

static uint16_t get_obj_id(vm_obj_h o) {
  if (!o) return VM_ID_NONE;
  if (vm_obj_is_dynamic(o)) {
    return vm_obj_dyn_get_id(o);
  }
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  for (uint16_t i = 0; i < g->count; i++) {
    if (g->items[i] == (void*)o) return i;
  }
  return VM_ID_NONE;
}

static bool is_already_emitted(uint16_t id) {
  for (uint16_t i = 0; i < s_emitted_count; i++) {
    if (s_emitted[i] == id) return true;
  }
  return false;
}

static void mark_emitted(uint16_t id) {
  if (s_emitted_count < MAX_EMITTED_PER_PASS) {
    s_emitted[s_emitted_count++] = id;
  }
}

static void frame_init(sub_frame_t* f) {
  f->buf[0] = VM_SUB_CLASS_HEADER;
  f->buf[1] = VM_SUB_PACKET_SET_DATA;
  f->buf[2] = 0;
  f->len = 3;
  f->count = 0;
}

static void frame_flush(sub_frame_t* f) {
  if (!f || f->count == 0) return;
  f->buf[2] = f->count;

  // Log telemetry packet details
  ESP_LOGI(TAG, "TX Telemetry: %u records (%u bytes)", (unsigned)f->count, (unsigned)f->len);
  size_t off = 3;
  for (uint8_t i = 0; i < f->count && (off + 6) <= f->len; i++) {
    uint16_t id = (uint16_t)(f->buf[off] | ((uint16_t)f->buf[off + 1] << 8));
    uint16_t start_idx = (uint16_t)(f->buf[off + 2] | ((uint16_t)f->buf[off + 3] << 8));
    uint16_t byte_len = (uint16_t)(f->buf[off + 4] | ((uint16_t)f->buf[off + 5] << 8));
    off += 6;
    if (off + byte_len <= f->len) {
      if (byte_len == 4) {
        float fval = 0.0f;
        memcpy(&fval, f->buf + off, 4);
        ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=4): float=%f", (unsigned)i, (unsigned)id, (unsigned)start_idx, (double)fval);
      } else if (byte_len == 1) {
        ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=1): val=%u", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)f->buf[off]);
      } else if (byte_len == 2) {
        uint16_t u16val = (uint16_t)(f->buf[off] | ((uint16_t)f->buf[off + 1] << 8));
        ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=2): u16=%u", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)u16val);
      } else {
        ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=%u B)", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)byte_len);
      }
      off += byte_len;
    }
  }

  // Format and log raw hex buffer
  char hex_buf[96];
  size_t hex_len = 0;
  for (size_t i = 0; i < f->len && hex_len + 3 < sizeof(hex_buf); i++) {
    hex_len += (size_t)snprintf(hex_buf + hex_len, sizeof(hex_buf) - hex_len, "%02X ", f->buf[i]);
  }
  ESP_LOGI(TAG, "  Frame Hex: [ %s%s]", hex_buf, (f->len * 3 >= sizeof(hex_buf)) ? "..." : "");

  if (s_sender) {
    (void)s_sender(f->buf, f->len);
  }
  frame_init(f);
}

static void frame_append_obj(sub_frame_t* f, uint16_t id, vm_obj_h o) {
  if (!o) return;

  uint16_t byte_len = 0;
  bool is_ptr = ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR);

  if (is_ptr) {
    uint16_t items = vm_obj_get_items_cnt(o);
    byte_len = (uint16_t)(items * 2u);
  } else {
    byte_len = o->head.payload_size;
  }

  // If adding this record exceeds maximum frame length, flush the current frame
  if (f->len + 6 + byte_len > VM_SUB_MAX_FRAME_LEN) {
    frame_flush(f);
  }

  // If a single record alone is larger than remaining space in an empty frame, cap to frame limit
  if (f->len + 6 + byte_len > VM_SUB_MAX_FRAME_LEN) {
    byte_len = (uint16_t)(VM_SUB_MAX_FRAME_LEN - f->len - 6);
  }

  uint8_t* p = f->buf + f->len;
  // u16 id
  p[0] = (uint8_t)(id & 0xFFu);
  p[1] = (uint8_t)((id >> 8) & 0xFFu);
  // u16 start_idx
  p[2] = 0;
  p[3] = 0;
  // u16 byte_len
  p[4] = (uint8_t)(byte_len & 0xFFu);
  p[5] = (uint8_t)((byte_len >> 8) & 0xFFu);

  if (is_ptr) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items && (i * 2u + 1u) < byte_len; i++) {
      uint16_t cid = get_obj_id(children[i]);
      p[6 + i * 2] = (uint8_t)(cid & 0xFFu);
      p[6 + i * 2 + 1] = (uint8_t)((cid >> 8) & 0xFFu);
    }
  } else {
    if (byte_len > 0) {
      memcpy(p + 6, o->payload, byte_len);
    }
  }

  f->len += 6 + byte_len;
  f->count++;
}

static bool tree_has_update(vm_obj_h o, int depth) {
  if (!o || depth > 8) return false;
  if (o->head.f.upd) return true;

  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items; i++) {
      if (tree_has_update(children[i], depth + 1)) return true;
    }
  }
  return false;
}

static void emit_tree(sub_frame_t* f, vm_obj_h o, int depth) {
  if (!o || depth > 8) return;

  uint16_t id = get_obj_id(o);
  if (id != VM_ID_NONE && !is_already_emitted(id)) {
    mark_emitted(id);
    frame_append_obj(f, id, o);
  }

  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items; i++) {
      emit_tree(f, children[i], depth + 1);
    }
  }
}

err_h vm_sub_init(void) {
  vm_exec_set_sample_hook(vm_sub_scan);
  return NULL;
}

void vm_sub_set_sender(vm_sub_sender_fn sender) {
  s_sender = sender;
}

vm_sub_sender_fn vm_sub_get_sender(void) {
  return s_sender;
}

err_h vm_sub_subscribe(const uint16_t* ids, uint16_t count) {
  if (count > VM_SUB_MAX_SUBSCRIBERS) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = count, .min = 0, .max = VM_SUB_MAX_SUBSCRIBERS);
  }

  s_sub_count = 0;
  for (uint16_t i = 0; i < count; i++) {
    s_subscribed_ids[s_sub_count++] = ids[i];
    ESP_LOGI(TAG, "  -> subscribed obj_id=%u", (unsigned)ids[i]);
  }

  ESP_LOGI(TAG, "subscribed to %u objects total", (unsigned)s_sub_count);
  return NULL;
}

err_h vm_sub_handle_packet(const uint8_t* body, size_t len) {
  SE_CHECK_NOT_NULL(body);
  if (len < 1) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  uint8_t count = body[0];
  if (count > 0 && len < (1u + (size_t)count * 2u)) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  if (count == 0) {
    vm_sub_reset();
    return NULL;
  }

  uint16_t ids[VM_SUB_MAX_SUBSCRIBERS];
  uint16_t actual_cnt = count;
  if (actual_cnt > VM_SUB_MAX_SUBSCRIBERS) {
    actual_cnt = VM_SUB_MAX_SUBSCRIBERS;
  }

  for (uint16_t i = 0; i < actual_cnt; i++) {
    ids[i] = (uint16_t)(body[1 + i * 2] | ((uint16_t)body[1 + i * 2 + 1] << 8));
  }

  return vm_sub_subscribe(ids, actual_cnt);
}

void vm_sub_scan(void) {
  if (!s_sender || s_sub_count == 0) return;

  s_emitted_count = 0;
  sub_frame_t frame;
  frame_init(&frame);

  for (uint16_t i = 0; i < s_sub_count; i++) {
    uint16_t id = s_subscribed_ids[i];
    vm_obj_h o = vm_obj_get_by_id(id);
    if (!o) continue;

    if (tree_has_update(o, 0)) {
      emit_tree(&frame, o, 0);
    }
  }

  frame_flush(&frame);
}

void vm_sub_reset(void) {
  s_sub_count = 0;
  s_emitted_count = 0;
  ESP_LOGI(TAG, "subscriptions cleared");
}

uint16_t vm_sub_count(void) {
  return s_sub_count;
}

const uint16_t* vm_sub_get_ids(uint16_t* out_count) {
  if (out_count) *out_count = s_sub_count;
  return s_subscribed_ids;
}
