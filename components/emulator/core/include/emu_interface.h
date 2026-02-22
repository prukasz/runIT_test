#pragma once 
#include "stdint.h"
#include "gatt_buff.h"
#include "emu_buffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
*@brief Freertos task to acces emulator 
*/
void emu_interface_task(void* params);


BaseType_t emu_interface_process_packet();

/** Register a callback invoked after each packet is fully processed.
 *  Intended to send a BLE ready-ACK so the peer knows it can send the next packet. */
void emu_interface_set_packet_done_cb(void (*cb)(void));


