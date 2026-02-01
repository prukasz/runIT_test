#include "common.h"
#include "gatt_svc.h"
#include "gatt_uuids.h"
#include "order_types.h"

extern QueueHandle_t emu_interface_orders_queue;

static chr_msg_buffer_t *emu_in_buffer = NULL;  // internal, not global outside this file
static chr_msg_buffer_t *emu_out_buffer = NULL; // internal, not global outside this file 

/*-----callback when characteristics is accesed----*/
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*----Characteristic value handle----*/
static uint16_t chr_val_handle_emu_out;
static uint16_t chr_val_handle_emu_in;
  
/*----Characteristic descriptor value handle----*/
static uint16_t chr_desc_val_handle_emu_out;
static uint16_t chr_desc_val_handle_emu_in;

/*----Characteristics connection handle----
Represents what device is accesing characteristics*/
static uint16_t chr_conn_handle_emu_out = 0;
static uint16_t chr_conn_handle_emu_in = 0;

/*User (readable) descriptor value as text do be displayed*/
static const char chr_desc_emu_out[] = "emulator data out channel";
static const char chr_desc_emu_in[]  = "emulator data in channel";


/*Indication status struct
Indication is like notification that requires confirmation of being receivied by peer*/
static indicate_status_t indicate_status_emu_out = {.ind_status = 0, .chr_conn_handle_status = 0};
static notify_status_t notify_status_emu_in  = {.notify_status = 0, .chr_conn_handle_status = 0};

static const struct ble_gatt_svc_def svc_1 ={
    .type = BLE_GATT_SVC_TYPE_PRIMARY, //type od service, secondary is like subsection(rarely used?)
        .uuid = &uuid_svc_emu.u,  //uuid of service
        .characteristics = (struct ble_gatt_chr_def[]) { //characteristics in this service 
            {
                .uuid = &uuid_chr_emu_out.u, //uuid of characteristic
                .access_cb = chr_access_cb, //callback to be used if accesed by peer
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE, //what characterisitc can do(permision)
                .val_handle = &chr_val_handle_emu_out,  //will store handle assigned by stack
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901), // User Description id
                        .access_cb = chr_desc_access_cb, //callback when desc accesed
                        .att_flags = BLE_ATT_F_READ,  //permision to read
                        .arg = (void*)chr_desc_emu_out, //passed to callback
                    },
                    {0} //zero is indication of list end
                }
            },

            { 
                .uuid = &uuid_chr_emu_in.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &chr_val_handle_emu_in,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901), // User Description
                        .access_cb = chr_desc_access_cb,
                        .att_flags = BLE_ATT_F_READ,
                        .arg = (void*)chr_desc_emu_in
                    },
                    {0}
                }
            },
            {0}
        }
};


/*This is main struct for definition of all services and characteristics used by NIMBLE*/
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {svc_1,{0}};

/*-----callback when characteristics is accesed----*/
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /*check what characteristics was accesed via comparing handle*/
    if (attr_handle == chr_val_handle_emu_out) {
        //read only option
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t *data_to_send = NULL;
            size_t len = 0;
            
            if (chr_msg_buffer_size(emu_out_buffer) > 0) {
                esp_err_t err = chr_msg_buffer_get(emu_out_buffer, 0, &data_to_send, &len);
                if (err == ESP_OK && data_to_send != NULL) {
                    // Append entire message - NimBLE handles MTU fragmentation
                    int rc = os_mbuf_append(ctxt->om, data_to_send, len);
                    if (rc != 0) {
                        return BLE_ATT_ERR_INSUFFICIENT_RES;
                    }
                    ESP_LOGI("GATT", "Sent %d bytes to device %d", len, conn_handle);
                }
            }
            return 0;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    else if (attr_handle == chr_val_handle_emu_in) {
        //write only
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            
        size_t len = OS_MBUF_PKTLEN(ctxt->om);
        if(len > 512){
            ESP_LOGW(TAG, "Received data length %d exceeds buffer limit", len);
        }
        uint8_t *temp = malloc(len);
        os_mbuf_copydata(ctxt->om, 0, len, temp);
        emu_order_t order_code = (emu_order_t)(temp[1] << 8) | temp[0];
        if (len == 2){
            xQueueSend(emu_interface_orders_queue,&order_code, pdMS_TO_TICKS(1000));
            chr_msg_buffer_add(emu_in_buffer, temp, len); 
        }else{chr_msg_buffer_add(emu_in_buffer, temp, len);}// do not fill buff with commands(add to queue)
        free(temp);
        // if (notify_status_emu_in.chr_conn_handle_status && notify_status_emu_in.notify_status) {
        //     ble_gatts_notify(chr_conn_handle_emu_in, chr_val_handle_emu_in);
        //     ESP_LOGI(TAG, "sent confirmation");
        //     }
        }//end if op    
        
        return 0;
    }// end if attr andle 
    return BLE_ATT_ERR_UNLIKELY;
}

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)

{
    const char *chr_user_desc = (char*)arg; //passed text value
    ESP_LOGI(TAG, "%s", chr_user_desc);
    return os_mbuf_append(ctxt->om, chr_user_desc, strlen(chr_user_desc)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; //copy descriptor to buffer

}

/*Indication wrapper (checks if possible)*/
void chr_send_indication(indicate_status_t *indicate_status, int16_t chr_conn_handle, int16_t chr_val_handle) {
    if (indicate_status->chr_conn_handle_status && indicate_status->ind_status) {
        ble_gatts_indicate(chr_conn_handle, chr_val_handle);
    }
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGI(TAG, "Characteristic registered: handle=0x%04x", ctxt->chr.val_handle);
            break;
        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGI(TAG, "Descriptor registered: handle=0x%04x", ctxt->dsc.handle);
            // Match descriptor by parent characteristic pointer
            if (ctxt->dsc.dsc_def->uuid->type == BLE_UUID_TYPE_16 &&
                ble_uuid_u16(ctxt->dsc.dsc_def->uuid) == 0x2901) { // User description
                if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_emu_out) {
                    chr_desc_val_handle_emu_out = ctxt->dsc.handle;
                } else if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_emu_in) {
                    chr_desc_val_handle_emu_in = ctxt->dsc.handle;
                }   
            }
            break;
        default:
            break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == chr_val_handle_emu_out) {
        chr_conn_handle_emu_out = event->subscribe.conn_handle;
        indicate_status_emu_out.chr_conn_handle_status = true;
        indicate_status_emu_out.ind_status = event->subscribe.cur_indicate; // true if indication enabled
    }

    if (event->subscribe.attr_handle == chr_val_handle_emu_in) {
        chr_conn_handle_emu_in = event->subscribe.conn_handle;
        notify_status_emu_in.chr_conn_handle_status = true;
        notify_status_emu_in.notify_status = event->subscribe.cur_notify;
    }
}

int gatt_svc_init(chr_msg_buffer_t *emu_in_buff, chr_msg_buffer_t *emu_out_buff) {
    ESP_LOGI(TAG, "gap_svc_init");
    emu_in_buffer = emu_in_buff;
    emu_out_buffer = emu_out_buff;
    ble_svc_gatt_init();
    RETURN_ON_ERROR(ble_gatts_count_cfg(gatt_svr_svcs));
    RETURN_ON_ERROR(ble_gatts_add_svcs(gatt_svr_svcs));
    return 0;
}

void send_indication(void){
    chr_send_indication(&indicate_status_emu_out, chr_conn_handle_emu_out, chr_val_handle_emu_out);
}



