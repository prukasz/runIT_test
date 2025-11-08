#pragma once
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gap.h"
#include "gatt_buff.h"
#include "emulator.h"


void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
int gatt_svc_init(chr_msg_buffer_t *rx_buffer);
                                 
typedef struct{         
    bool ind_status;
    bool chr_conn_handle_status;
}indicate_status_t;

typedef struct{         
    bool notify_status;
    bool chr_conn_handle_status;
}notify_status_t;

void send_indication();
void chr_send_indication(indicate_status_t *indicate_status, int16_t chr_conn_handle, int16_t chr_val_handle);

