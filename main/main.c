/* Includes */
#include "gap.h"
#include "gatt_svc.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "i2c_tasks.h"
#include "driver/i2c_master.h"
#include "servo_manager.h"

#define TAG "MAIN"
#define I2C_SCL_NUM GPIO_NUM_21
#define I2C_SDA_NUM GPIO_NUM_22

pca9685_handle_t pca9685;
pcf8575_handle_t pcf8575;
ads1115_handle_t ads1115;
mpu6050_handle_t mpu6050;

static void i2c_dev_cfg(void);
void ble_store_config_init(void);
static void on_stack_reset(int reason); // Called on BLE stack reset
static void on_stack_sync(void);        // Called when stack syncs with controller
static void nimble_host_config_init(void); // Initialize NimBLE host callbacks
static void nimble_host_task(void *param); // NimBLE host task loop
static void notify_task(void *param);  // Heart rate update task

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

void app_main(void) {
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
    gatt_svc_init();

    // Configure NimBLE host callbacks
    nimble_host_config_init();  
    i2c_dev_cfg();
    xTaskCreate(task_ads1115, "ads", 2048, (void*)&ads1115, 1, NULL);
    xTaskCreate(task_pca9685, "pca", 2048, (void*)&pca9685, 1, NULL);
    xTaskCreate(task_mpu6050, "mpu", 2048, (void*)&mpu6050, 1, NULL);
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 5, NULL);
    xTaskCreate(notify_task, "notify", 4*1024, NULL, 1, NULL);
    servo_manager_init();
    ESP_ERROR_CHECK(servo_manager_add(1, 1));
    ESP_ERROR_CHECK(servo_manager_configure(1,1250,90,45, 90, 0, 0));
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        servo_manager_set_angle (1,0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        servo_manager_neutral(1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        servo_manager_set_angle (1,90);
    }
}

static void i2c_dev_cfg(void){
    static i2c_master_bus_handle_t i2c_master_bus;
    static i2c_master_bus_config_t i2c_master_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_NUM,
        .sda_io_num = I2C_SDA_NUM,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_bus_config, &i2c_master_bus));
    ESP_ERROR_CHECK(ads1115_init(&ads1115, &i2c_master_bus, ADS1115_I2C_DEFAULT_ADDR+1));
    ESP_ERROR_CHECK(pca9685_init(&pca9685, &i2c_master_bus, 0x40));
    ESP_ERROR_CHECK(pcf8575_init(&pcf8575, &i2c_master_bus, PCF8575_DEFAULT_ADDRESS));
    ESP_ERROR_CHECK(mpu6050_init(&mpu6050, &i2c_master_bus, MPU6050_I2C_ADDRESS));
}

static void notify_task(void *param) {
    (void)param;
    TickType_t time;
    while (1) {
        send_indication();
        xTaskDelayUntil(&time, pdMS_TO_TICKS(200));
    }
    vTaskDelete(NULL);
}