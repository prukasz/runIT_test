#pragma once 
#include "stdint.h"
#include "gatt_buff.h"
#include "emu_types.h"
#include "emu_parse.h"
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


