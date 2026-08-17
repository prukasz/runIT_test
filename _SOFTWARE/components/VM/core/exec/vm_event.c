#include "vm_event.h"
#include "utils.h"

#define OWNER OWNER_VM_EXEC

R_QUEUE_DEFINE(s_vm_event_q, VM_EVENT_DEPTH, sizeof(cb_event_t));

/* Scan cycle snapshot. Written by vm_event_drain(), read lock-free by supervisor/blocks. */
static cb_event_t s_cycle[VM_EVENT_DEPTH];
static uint8_t s_cycle_cnt;

static volatile uint16_t s_dropped;
static volatile uint16_t s_drop_type;

bool vm_event_post(const cb_event_t* ev) {
  if (!ev) return false;

  if (R_QUEUE_SEND(s_vm_event_q, ev, NO_WAIT) == pdTRUE) return true;

  if (s_dropped != UINT16_MAX) s_dropped++;
  s_drop_type = ev->head.callback_type;
  return false;
}

static void vm_event_route(const cb_event_t* ev) {
  (void)vm_event_post(ev);
}

__attribute__((constructor)) static void vm_event_route_register(void) {
  (void)sys_cb_register_route(SYS_CB_ROUTE_VM, vm_event_route);
}

void vm_event_drain(void) {
  s_cycle_cnt = 0;
  while (s_cycle_cnt < VM_EVENT_DEPTH && R_QUEUE_RECEIVE(s_vm_event_q, &s_cycle[s_cycle_cnt], NO_WAIT)) {
    s_cycle_cnt++;
  }
}

uint8_t vm_event_count(void) {
  return s_cycle_cnt;
}

const cb_event_t* vm_event_snapshot(uint8_t* out_cnt) {
  if (out_cnt) *out_cnt = s_cycle_cnt;
  return s_cycle_cnt ? s_cycle : NULL;
}

const cb_event_t* vm_event_at(uint8_t i) {
  return (i < s_cycle_cnt) ? &s_cycle[i] : NULL;
}

err_h vm_event_take_overflow(void) {
  uint16_t dropped = s_dropped;
  if (dropped == 0) return NULL;
  s_dropped = 0;
  SE_RET_ERR(ERR_VM_EVENT_OVERFLOW, .type = s_drop_type, .depth = VM_EVENT_DEPTH, .dropped = dropped);
}

void vm_event_reset(void) {
  s_cycle_cnt = 0;
  xQueueReset(s_vm_event_q);
  s_dropped = 0;
}
