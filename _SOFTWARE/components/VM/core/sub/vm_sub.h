#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include "vm_obj.h"

/**
 * @file vm_sub.h
 * @brief VM object subscription and telemetry mechanism.
 *
 * Allows clients to subscribe to a list of object IDs via inbound packet 0x47.
 * During each scan cycle, vm_sub_scan() checks subscribed objects.
 * If an object (or any node in its nested VM_OBJ_PTR tree) is updated (head.f.upd == 1),
 * a reverse 0x43 packet (matching decoder_packet_vm_set_data layout) is generated and
 * dispatched via the registered sender callback.
 */

#define VM_SUB_MAX_SUBSCRIBERS 128
#define VM_SUB_CLASS_HEADER 0x04
#define VM_SUB_PACKET_SUBSCRIBE 0x47
#define VM_SUB_PACKET_SET_DATA 0x43
#define VM_SUB_MAX_FRAME_LEN 512

typedef err_h (*vm_sub_sender_fn)(const uint8_t* data, size_t len);

/**
 * @brief Initialize subscription subsystem and hook into vm_exec sample point.
 * @return err_h NULL on success.
 */
err_h vm_sub_init(void);

/**
 * @brief Set the sender callback that delivers outbound reverse 0x43 packets.
 */
void vm_sub_set_sender(vm_sub_sender_fn sender);

/**
 * @brief Get the currently registered sender callback.
 */
vm_sub_sender_fn vm_sub_get_sender(void);

/**
 * @brief Set the list of subscribed object IDs.
 *
 * @param ids Array of object IDs to subscribe to.
 * @param count Number of IDs in the array. 0 clears all subscriptions.
 * @return err_h NULL on success, or error on invalid parameter/capacity.
 */
err_h vm_sub_subscribe(const uint16_t* ids, uint16_t count);

/**
 * @brief Process an inbound subscription packet payload (body after packet header 0x47).
 *
 * Wire format:
 *   [0]     u8 count
 *   [1..]   count * u16 obj_id (little-endian)
 *
 * @param body Pointer to packet body.
 * @param len Length of body in bytes.
 * @return err_h NULL on success, or error code.
 */
err_h vm_sub_handle_packet(const uint8_t* body, size_t len);

/**
 * @brief Scan all subscribed objects for updates and emit reverse 0x43 packets.
 *        Called automatically each pass via vm_exec's sample hook before upd is cleared.
 */
void vm_sub_scan(void);

/**
 * @brief Clear all active subscriptions.
 */
void vm_sub_reset(void);

/**
 * @brief Returns the number of currently subscribed objects.
 */
uint16_t vm_sub_count(void);

/**
 * @brief Inspect the currently subscribed IDs.
 *
 * @param out_count Optional pointer to receive count.
 * @return Pointer to internal array of subscribed IDs.
 */
const uint16_t* vm_sub_get_ids(uint16_t* out_count);
