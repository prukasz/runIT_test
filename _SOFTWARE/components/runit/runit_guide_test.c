#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "esp_log.h"
#include "runit.h"
#include "runit_board_defs.h"
#include "sys_ble.h"
#include "sys_error.h"
#include "vm_obj.h"
#include "vm_obj_access.h"
#include "vm_store.h"
#include "vm_exec.h"

static const char* TAG = "guide_test";

typedef struct {
  const char*    name;
  const uint8_t* data;
  size_t         len;
} guide_packet_t;

void runit_test_guide_pipeline(void) {
  ESP_LOGW(TAG, "===============================================================");
  ESP_LOGW(TAG, "  STARTING VM BOOT-TO-RUN GUIDE PIPELINE TEST");
  ESP_LOGW(TAG, "  Testing 3-block pipeline: FOR loop, EXPR math, SET store");
  ESP_LOGW(TAG, "  Injecting wire packets into BLE RX stream (SYS_BLE_CHR_RUNIT_RX)");
  ESP_LOGW(TAG, "===============================================================");

  // Let boot and background receiver tasks fully settle
  vTaskDelay(pdMS_TO_TICKS(300));

  // Packet 1: Reset (0x40)
  static const uint8_t pkt1_reset[] = {0x04, 0x40};

  // Packet 2: Open Program Container (0x41)
  static const uint8_t pkt2_open[] = {0x04, 0x41, 0x05, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x08, 0x00, 0x00};

  // Packet 3: Define Objects (0x42)
  static const uint8_t pkt3_add_objs[] = {
      0x04, 0x42, 0x05,
      0x00, 0x00, 0x04, 0x00, 0x05, 0x03,  // Obj 0 (Sum, float, 4B)
      0x01, 0x00, 0x04, 0x00, 0x05, 0x03,  // Obj 1 (i, float, 4B)
      0x02, 0x00, 0x04, 0x00, 0x05, 0x03,  // Obj 2 (Math, float, 4B)
      0x03, 0x00, 0x01, 0x00, 0x06, 0x03,  // Obj 3 (ENO math, bool, 1B)
      0x04, 0x00, 0x01, 0x00, 0x06, 0x03   // Obj 4 (ENO set, bool, 1B)
  };

  // Packet 4: Seed Data (0x43) - Seed Obj 0 with initial float 0.0f
  static const uint8_t pkt4_seed_data[] = {
      0x04, 0x43, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  // Packet 5: Register Accessors (0x44)
  // Note: VM_IDX_LITERAL is 0x00 (0x01 is VM_IDX_REF, which caused infinite self-recursion)
  static const uint8_t pkt5_add_acc[] = {
      0x04, 0x44, 0x03,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,  // Acc 0 -> Obj 0[0]
      0x01, 0x00, 0x01, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,  // Acc 1 -> Obj 1[0]
      0x02, 0x00, 0x02, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00   // Acc 2 -> Obj 2[0]
  };

  // Packet 6: Add Block 0 (FOR Loop Controller) (0x45)
  static const uint8_t pkt6_block0[] = {
      0x04, 0x45, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0xFF, 0xFF,
      0x01, 0x00,  // Output Obj ID: 1
      0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0xA0, 0x40, 0x00, 0x00, 0x80, 0x3F,
      0x0A, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
  };

  // Packet 7: Add Block 1 (EXPR RPN Math: Sum + i) (0x45)
  // custom_len = 9 bytes (const_cnt(1) + rt(1) + code_len(2) + 5 bytes bytecode: IN 0 [2B], IN 1 [2B], ADD [1B])
  static const uint8_t pkt7_block1[] = {
      0x04, 0x45, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x03, 0x00,
      0x00, 0x00, 0x01, 0x00,  // Inputs: Acc 0, Acc 1
      0x02, 0x00,              // Output: Obj 2
      0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x01, 0x01, 0x06
  };

  // Packet 8: Add Block 2 (SET Assignment: Obj 2 -> Obj 0) (0x45)
  static const uint8_t pkt8_block2[] = {
      0x04, 0x45, 0x02, 0x00, 0x02, 0x00, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
      0x02, 0x00, 0x00, 0x00   // Source: Acc 2, Destination: Acc 0
  };

  // Packet 9: Subscribe Live Telemetry (Obj 0, Obj 1) (0x47)
  static const uint8_t pkt9_sub[] = {
      0x04, 0x47, 0x02, 0x00, 0x00, 0x01, 0x00
  };

  // Packet 10: Execution Start (VM_EXEC_NORMAL_MODE) (0x48)
  static const uint8_t pkt10_exec[] = {
      0x04, 0x48, 0x05
  };

  // Stop packet: Execution Stop (VM_EXEC_STOP) (0x48)
  static const uint8_t pkt_stop[] = {
      0x04, 0x48, 0x02
  };

  const guide_packet_t packets[] = {
      {"1. VM Reset (0x40)", pkt1_reset, sizeof(pkt1_reset)},
      {"2. VM Open Container (0x41)", pkt2_open, sizeof(pkt2_open)},
      {"3. Add Objects Batch (0x42)", pkt3_add_objs, sizeof(pkt3_add_objs)},
      {"4. Seed Initial Data (0x43)", pkt4_seed_data, sizeof(pkt4_seed_data)},
      {"5. Add Accessors Batch (0x44)", pkt5_add_acc, sizeof(pkt5_add_acc)},
      {"6. Add Block 0 FOR Loop (0x45)", pkt6_block0, sizeof(pkt6_block0)},
      {"7. Add Block 1 EXPR Math (0x45)", pkt7_block1, sizeof(pkt7_block1)},
      {"8. Add Block 2 SET Assignment (0x45)", pkt8_block2, sizeof(pkt8_block2)},
      {"9. Subscribe Live Telemetry (0x47)", pkt9_sub, sizeof(pkt9_sub)},
  };

  size_t pkt_cnt = sizeof(packets) / sizeof(packets[0]);

  for (size_t i = 0; i < pkt_cnt; i++) {
    ESP_LOGI(TAG, "-> Injecting [%s] (%u bytes)", packets[i].name, (unsigned)packets[i].len);
    err_h err = sys_ble_char_rx_inject(SYS_BLE_CHR_RUNIT_RX, packets[i].data, packets[i].len);
    if (SE_IS_ERR(err)) {
      ESP_LOGE(TAG, "Failed to inject packet [%s]", packets[i].name);
      return;
    }
    // Give receiver task time to dequeue and decode before next frame
    vTaskDelay(pdMS_TO_TICKS(40));
  }

  ESP_LOGI(TAG, "-> Injecting [10. Execution Start 0x48 0x05 (VM_EXEC_NORMAL_MODE)] (%u bytes)", (unsigned)sizeof(pkt10_exec));
  err_h err = sys_ble_char_rx_inject(SYS_BLE_CHR_RUNIT_RX, pkt10_exec, sizeof(pkt10_exec));
  if (SE_IS_ERR(err)) {
    ESP_LOGE(TAG, "Failed to inject execution start packet");
    return;
  }

  // Wait for supervisor pass and telemetry emission
  vTaskDelay(pdMS_TO_TICKS(400));

  // Inspect VM store directly to verify state
  vm_obj_h o0 = vm_obj_get_by_id(0);
  vm_obj_h o1 = vm_obj_get_by_id(1);
  vm_obj_h o2 = vm_obj_get_by_id(2);
  vm_obj_h o3 = vm_obj_get_by_id(3);
  vm_obj_h o4 = vm_obj_get_by_id(4);
  float val0 = 0.0f, val1 = 0.0f, val2 = 0.0f;
  uint8_t eno3 = o3 ? o3->payload[0] : 0;
  uint8_t eno4 = o4 ? o4->payload[0] : 0;
  if (o0 && o0->head.payload_size >= sizeof(float)) memcpy(&val0, o0->payload, sizeof(float));
  if (o1 && o1->head.payload_size >= sizeof(float)) memcpy(&val1, o1->payload, sizeof(float));
  if (o2 && o2->head.payload_size >= sizeof(float)) memcpy(&val2, o2->payload, sizeof(float));

  ESP_LOGW(TAG, "===============================================================");
  ESP_LOGW(TAG, "  VM OBJECT STORE SNAPSHOT (Direct verification):");
  ESP_LOGW(TAG, "    Obj 0 [Sum]      : %f (Expected: >= 15.0)", (double)val0);
  ESP_LOGW(TAG, "    Obj 1 [i]        : %f (Expected: 5.0)", (double)val1);
  ESP_LOGW(TAG, "    Obj 2 [Math]     : %f (Expected: >= 15.0)", (double)val2);
  ESP_LOGW(TAG, "    Obj 3 [ENO Math] : %u (Expected: 1)", (unsigned)eno3);
  ESP_LOGW(TAG, "    Obj 4 [ENO Set]  : %u (Expected: 1)", (unsigned)eno4);
  ESP_LOGW(TAG, "===============================================================");

  // Stop execution via BLE RX stream
  ESP_LOGI(TAG, "-> Injecting [Execution Stop 0x48 0x02 (VM_EXEC_STOP)]");
  (void)sys_ble_char_rx_inject(SYS_BLE_CHR_RUNIT_RX, pkt_stop, sizeof(pkt_stop));
  vTaskDelay(pdMS_TO_TICKS(50));

  ESP_LOGW(TAG, "  GUIDE PIPELINE TEST COMPLETED SUCCESSFULLY.");
  ESP_LOGW(TAG, "===============================================================");
}
