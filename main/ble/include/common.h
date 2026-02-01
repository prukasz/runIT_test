#pragma once

#include <stdbool.h>
#include <string.h>

/* ESP APIs */
#include "esp_log.h"
#include "sdkconfig.h"

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* NimBLE stack APIs */
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/* Defines */
#define TAG "NimBLE_GATT_Server"
#define DEVICE_NAME "SKIBIDI"

#define RETURN_ON_ERROR(x) do { \
    int __err = (x);          \
    if (__err != 0) return __err; \
} while (0)

