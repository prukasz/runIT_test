#include "gap.h"
#include "common.h"
#include "gatt_svc.h"

static int ble_gap_advertising_start(void);  
static int ble_gap_configure_advertising(void);                          // Starts BLE advertising
static int gap_event_handler(struct ble_gap_event *event, void *arg); // Handles GAP events

/* Private variables */
static uint8_t own_addr_type;
static bool adv_configured = 0;
static uint8_t addr_val[6] = {0};
static uint8_t esp_uri[] = {
    BLE_GAP_URI_PREFIX_HTTPS, 
    't','e','x','t'
};

static int ble_gap_configure_advertising(void){  
    
    const char *name;
    struct ble_hs_adv_fields adv_fields  = {0};  
    struct ble_hs_adv_fields rsp_fields  = {0};

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    name = ble_svc_gap_device_name();  
    adv_fields.name = (uint8_t *)name; //set name 
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
    adv_fields.appearance_is_present = 1;
    adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
    adv_fields.le_role_is_present = 1;
    RETURN_ON_ERROR(ble_gap_adv_set_fields(&adv_fields));
    /**********************/
    rsp_fields.device_addr = addr_val;
    rsp_fields.device_addr_type = own_addr_type;
    rsp_fields.device_addr_is_present = 1;
    rsp_fields.uri = esp_uri;
    rsp_fields.uri_len = sizeof(esp_uri);
        rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
    rsp_fields.adv_itvl_is_present = 1;
    adv_configured = true;
    return ble_gap_adv_rsp_set_fields(&rsp_fields);

}

static int ble_gap_advertising_start(void) {
    if (!adv_configured){
        ESP_LOGW(TAG, "configureing adv first");
        RETURN_ON_ERROR(ble_gap_configure_advertising());
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; //any centrall can connect
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; //visible to all
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(500); //advertising range
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(510);
    return ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg) {

    struct ble_gap_conn_desc desc; 

    switch (event->type) {  
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {//aka success
            RETURN_ON_ERROR(ble_gap_conn_find(event->connect.conn_handle, &desc));//retrieve details of conncetion into descriptor
            struct ble_gap_upd_params params = { //update paremeters
                .itvl_min = desc.conn_itvl,
                .itvl_max = desc.conn_itvl,
                .latency = 3, //3 skipped conncetions max
                .supervision_timeout = desc.supervision_timeout
            };
            return ble_gap_update_params(event->connect.conn_handle, &params);
        } 
        else { return ble_gap_advertising_start();} //start adv if fail

    case BLE_GAP_EVENT_DISCONNECT:
        return ble_gap_advertising_start(); //if device disconnect start advertising again
    case BLE_GAP_EVENT_CONN_UPDATE:
        return ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    case BLE_GAP_EVENT_ADV_COMPLETE: //if adv time ended
        return ble_gap_advertising_start();
    case BLE_GAP_EVENT_NOTIFY_TX:  //when notify transmision is finished
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: //if subscribed to characteristics cccd
        gatt_svr_subscribe_cb(event);
        return 0;
    case BLE_GAP_EVENT_MTU:
         ESP_LOGI(TAG, "Negotiated MTU: conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
            return 0;
    }
    return 0;
}

/* Public functions */
int ble_gap_advertising_init(void) { // Initializes device address and starts advertising
    RETURN_ON_ERROR(ble_hs_util_ensure_addr(0)); //ensures has valid address (bt MAC), if not set genetate
    RETURN_ON_ERROR(ble_hs_id_infer_auto(0, &own_addr_type)); //best addr type for advertising     
    RETURN_ON_ERROR(ble_hs_id_copy_addr(own_addr_type, addr_val, NULL)); //copy addres for reuse
    RETURN_ON_ERROR(ble_gap_configure_advertising());
    return ble_gap_advertising_start();
}

int ble_gap_configure(void) { 
    ble_svc_gap_init();
    ble_att_set_preferred_mtu(517);
    return ble_svc_gap_device_name_set(DEVICE_NAME);
}
