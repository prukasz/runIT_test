#pragma once 
#include "stdint.h"
#include "gatt_buff.h"
#include "emulator_types.h"
#include "emulator_parse.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
*@brief Add ble characteristic buffer to emulator
*@param msg source buffer
*/
emu_result_t emu_parse_source_add(chr_msg_buffer_t * msg);

/**
*@brief Freertos task to acces emulator 
*/
void emu_interface_task(void* params);

