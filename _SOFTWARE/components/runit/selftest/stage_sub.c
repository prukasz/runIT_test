#include "selftest_frame.h"
#include "vm_exec.h"
#include "vm_sub.h"

static uint8_t s_mock_sub_buf[512];
static size_t s_mock_sub_len;
static int s_mock_sub_calls;

static err_h mock_sub_sender(const uint8_t* data, size_t len) {
  s_mock_sub_calls++;
  s_mock_sub_len = (len < sizeof(s_mock_sub_buf)) ? len : sizeof(s_mock_sub_buf);
  memcpy(s_mock_sub_buf, data, s_mock_sub_len);
  return NULL;
}

void test_subscription(void) {
  ESP_LOGI(TAG, "-- Subscription & telemetry tests --");

  vm_loader_reset();
  vm_sub_reset();
  (void)vm_sub_init();
  vm_sub_sender_fn prev_sender = vm_sub_get_sender();
  vm_sub_set_sender(mock_sub_sender);
  s_mock_sub_calls = 0;
  s_mock_sub_len = 0;

  // 1. Setup program with 3 objects: OBJ 1 (float), OBJ 2 (u32), OBJ 3 (VM_OBJ_PTR[2] linking 1 & 2)
  ck("sub: open program", upload_open(4, 4, 1, 1024) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(3);
  add_obj_record(1, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE | VM_LOAD_F_UPD_RESETABLE, "val_f");
  add_obj_record(2, 1, VM_OBJ_U32, VM_LOAD_F_MUTABLE | VM_LOAD_F_UPD_RESETABLE, "val_u");
  add_obj_record(3, 2, VM_OBJ_PTR, VM_LOAD_F_MUTABLE | VM_LOAD_F_UPD_RESETABLE, "tree");
  ck("sub: create objects", f_send() == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x43);
  f_u8(3);
  f_u16(1); f_u16(0); f_u16(4); f_f32(10.5f);
  f_u16(2); f_u16(0); f_u16(4); f_u32(100);
  f_u16(3); f_u16(0); f_u16(4); f_u16(1); f_u16(2);
  ck("sub: set initial data", f_send() == NULL);

  // Clear initial upd flags
  vm_exec_pass();
  s_mock_sub_calls = 0;

  // 2. Subscribe to OBJ 1 via 0x47 frame
  f_begin(VM_LOADER_CLASS_HEADER, 0x47);
  f_u8(1);
  f_u16(1);
  ck("sub: 0x47 subscribe to obj 1", f_send() == NULL && vm_sub_count() == 1);

  // 3. Scan without updates -> no telemetry
  vm_exec_pass();
  ck("sub: no telemetry when not updated", s_mock_sub_calls == 0);

  // 4. Update OBJ 1 -> telemetry emitted
  vm_obj_h o1 = vm_obj_by_id(1);
  ck("sub: obj 1 exists", o1 != NULL);
  if (o1) {
    *(float*)o1->payload = 25.5f;
    o1->head.f.upd = 1;
  }
  vm_exec_pass();
  ck("sub: telemetry sent on update", s_mock_sub_calls == 1);
  ck("sub: class header 0x04", s_mock_sub_len >= 3 && s_mock_sub_buf[0] == 0x04);
  ck("sub: packet header 0x43", s_mock_sub_len >= 3 && s_mock_sub_buf[1] == 0x43);
  ck("sub: 1 record emitted", s_mock_sub_len >= 3 && s_mock_sub_buf[2] == 1);

  // Validate record 0: id=1, start_idx=0, byte_len=4, data=25.5f
  if (s_mock_sub_len >= 13) {
    uint16_t rid = (uint16_t)(s_mock_sub_buf[3] | ((uint16_t)s_mock_sub_buf[4] << 8));
    uint16_t rlen = (uint16_t)(s_mock_sub_buf[7] | ((uint16_t)s_mock_sub_buf[8] << 8));
    float rval = 0.0f;
    memcpy(&rval, s_mock_sub_buf + 9, 4);
    ck("sub: record matches obj 1 payload", rid == 1 && rlen == 4 && near_f(rval, 25.5f));
  }

  // 5. Subsequent pass without updates -> no telemetry
  vm_exec_pass();
  ck("sub: no telemetry on second pass", s_mock_sub_calls == 1);

  // 6. Test nested object subscription (OBJ 3)
  f_begin(VM_LOADER_CLASS_HEADER, 0x47);
  f_u8(1);
  f_u16(3);
  ck("sub: subscribe to nested obj 3", f_send() == NULL && vm_sub_count() == 1);

  // Update child OBJ 2 inside tree
  vm_obj_h o2 = vm_obj_by_id(2);
  ck("sub: obj 2 exists", o2 != NULL);
  if (o2) {
    *(uint32_t*)o2->payload = 200;
    o2->head.f.upd = 1;
  }
  vm_exec_pass();
  ck("sub: nested tree update triggered telemetry", s_mock_sub_calls == 2);
  ck("sub: all 3 tree objects emitted", s_mock_sub_buf[2] == 3);

  // 7. Test unsubscription (0x47 count 0)
  f_begin(VM_LOADER_CLASS_HEADER, 0x47);
  f_u8(0);
  ck("sub: unsubscribe all via count 0", f_send() == NULL && vm_sub_count() == 0);

  if (o1) {
    *(float*)o1->payload = 99.0f;
    o1->head.f.upd = 1;
  }
  vm_exec_pass();
  ck("sub: no telemetry after unsubscribe", s_mock_sub_calls == 2);

  vm_sub_reset();
  vm_loader_reset();
  vm_sub_set_sender(prev_sender);
}
