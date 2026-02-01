#pragma once

#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

int ble_gap_advertising_init(void);
int ble_gap_configure(void);
