#include "gap.h"
#include "gatt_svc.h"
#include "gatt_buff.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "i2c_tasks.h"
#include "driver/i2c_master.h"
#include "servo_manager.h"
#include "esc_manager.h"
#include "emulator_loop.h"
#include "emulator.h"
#include "emulator_types.h"



TaskHandle_t main_task;

#define TAG "MAIN"
#define I2C_SCL_NUM GPIO_NUM_21
#define I2C_SDA_NUM GPIO_NUM_22
void ble_store_config_init(void);
static void on_stack_reset(int reason); // Called on BLE stack reset
static void on_stack_sync(void);        // Called when stack syncs with controller
static void nimble_host_config_init(void); // Initialize NimBLE host callbacks
static void nimble_host_task(void *param); // NimBLE host task loop
static void on_stack_reset(int reason){
    ESP_LOGW(TAG, "stack reset reason: %d", reason);
}

static void on_stack_sync(void) {
    ble_gap_advertising_init();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb          = on_stack_reset;
    ble_hs_cfg.sync_cb           = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

void chr_msg_buffer_print(const chr_msg_buffer_t *buf) {
    const ble_msg_node_t *node = buf->head;
    int8_t index = 0;

    if (!node) {
        ESP_LOGI(TAG, "Buffer empty.");
        return;
    }

    while (node) {
        ESP_LOGI(TAG, "Message %d (len=%d):", (int)index, node->len);
        ESP_LOG_BUFFER_HEXDUMP(TAG, node->data, node->len, ESP_LOG_INFO);
        node = node->next;
        index++;
    }
}



void app_main(void) {

    static chr_msg_buffer_t emu_in_buffer;
    chr_msg_buffer_init(&emu_in_buffer);
    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ret = nimble_port_init();
    ESP_ERROR_CHECK(ret);

    // Initialize GAP and GATT services
    ble_gap_configure();
    gatt_svc_init(&emu_in_buffer);
    
    // Configure NimBLE host callbacks
    nimble_host_config_init();  
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 3, NULL);
    xTaskCreate(emu, "emu", 4*1024, NULL, 2, NULL);
    main_task = xTaskGetCurrentTaskHandle();
    
    emulator_parse_source_add(&emu_in_buffer);

    while(1){
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!LOOP_STATUS_CMP(LOOP_RUNNING))
        chr_msg_buffer_print(&emu_in_buffer);
        taskYIELD();
    }
}


