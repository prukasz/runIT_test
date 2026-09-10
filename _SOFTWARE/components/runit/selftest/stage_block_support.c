#include "selftest_harness.h"
#include "vm_block_build.h"
#include "vm_block_timer.h"
#include "vm_block_edge.h"
#include "vm_block_for.h"

/* Deterministic state-machine checks: no sleeps or wall-clock assumptions. */
void test_block_support(void) {
  vm_block_timer_data_t timer;
  vm_block_timer_init_data(&timer, VM_TIMER_TON, 10, false);
  ck("TON starts low", !vm_timer_step(&timer, true, 10, 100));
  ck("TON remains low before PT", !vm_timer_step(&timer, true, 10, 109));
  ck("TON reaches PT", vm_timer_step(&timer, true, 10, 110) && timer.elapsed_ms == 10);
  ck("TON resets with IN", !vm_timer_step(&timer, false, 10, 111) && timer.elapsed_ms == 0);
  ck("TON zero preset is immediate", vm_timer_step(&timer, true, 0, 112));

  vm_block_timer_init_data(&timer, VM_TIMER_TOF, 10, false);
  ck("TOF follows high IN", vm_timer_step(&timer, true, 10, 100));
  ck("TOF holds falling IN", vm_timer_step(&timer, false, 10, 101));
  ck("TOF expires", !vm_timer_step(&timer, false, 10, 111) && timer.elapsed_ms == 10);

  vm_block_timer_init_data(&timer, VM_TIMER_TP, 10, false);
  ck("TP first sample seeds history", !vm_timer_step(&timer, true, 10, 100));
  (void)vm_timer_step(&timer, false, 10, 101);
  ck("TP starts on rising edge", vm_timer_step(&timer, true, 10, 102));
  ck("TP ignores falling IN while running", vm_timer_step(&timer, false, 10, 103));
  ck("TP does not retrigger while running", vm_timer_step(&timer, true, 10, 104) && timer.start_ms == 102);
  ck("TP expires", !vm_timer_step(&timer, true, 10, 112));
  vm_block_timer_init_data(&timer, VM_TIMER_TON_INV, 10, false);
  ck("inverted wire mode", vm_timer_step(&timer, false, 10, 100));
  vm_block_timer_init_data(&timer, VM_TIMER_TON_INV, 10, true);
  ck("wire inversion and flag preserve XOR", !vm_timer_step(&timer, false, 10, 100));

  vm_block_edge_data_t edge;
  vm_edge_val_u th = {.u = 1};
  uint64_t sample = UINT64_MAX - 1;
  vm_payload_t p = {.ptr = &sample, .type = VM_OBJ_U64, .count = 1};
  bool fired = true;
  vm_edge_init(&edge, VM_EDGE_RISING, th);
  ck("edge first sample seeds history", !vm_edge_step(&edge, p, th, &fired) && !fired);
  sample = UINT64_MAX;
  ck("edge retains U64 precision", !vm_edge_step(&edge, p, th, &fired) && fired);
  sample = 0;
  ck("rising edge does not underflow", !vm_edge_step(&edge, p, th, &fired) && !fired);
  edge.edge_type = VM_EDGE_FALLING;
  edge.prev_val.u = UINT64_MAX;
  ck("falling edge spans U64 range", !vm_edge_step(&edge, p, th, &fired) && fired);
  int32_t signed_sample = INT32_MIN;
  p = (vm_payload_t){.ptr = &signed_sample, .type = VM_OBJ_I32, .count = 1};
  th.i = 1;
  vm_edge_init(&edge, VM_EDGE_BOTH, th);
  (void)vm_edge_step(&edge, p, th, &fired);
  signed_sample = INT32_MAX;
  ck("signed edge spans I32 range", !vm_edge_step(&edge, p, th, &fired) && fired);
  float fractional = 0.25f;
  p = (vm_payload_t){.ptr = &fractional, .type = VM_OBJ_F, .count = 1};
  th.f = 0.5f;
  vm_edge_init(&edge, VM_EDGE_BOTH, th);
  (void)vm_edge_step(&edge, p, th, &fired);
  fractional = 0.5f;
  ck("float edge below threshold", !vm_edge_step(&edge, p, th, &fired) && !fired);
  fractional = 1.0f;
  ck("float edge at threshold", !vm_edge_step(&edge, p, th, &fired) && fired);

  direct_arena_reset();
  vm_obj_h signal = mk(0, VM_OBJ_F, 1, NULL, true);
  vm_obj_h gate = mk(1, VM_OBJ_B, 1, NULL, true);
  vm_obj_h output = mk(2, VM_OBJ_B, 1, NULL, true);
  vm_obj_h eno = mk(3, VM_OBJ_B, 1, NULL, true);
  vm_accessor_t *sig_acc = NULL, *gate_acc = NULL, *bad_acc = NULL;
  ck("signal accessor", !vm_accessor_create(&sig_acc, 0, 0, 0));
  ck("enable accessor", !vm_accessor_create(&gate_acc, 1, 1, 0));
  ck("unresolved dynamic accessor", !vm_accessor_create(&bad_acc, 2, 31, 0));
  vm_block_h b = NULL;
  ck("timer block build", !vm_block_create(&b, 0, &(vm_block_cfg_t){
      .block_idx = 0, .in_cnt = 2, .q_cnt = 1, .en_cnt = 1,
      .custom_len = sizeof(timer), .in_acc_ids = (const uint16_t[]){0, VM_BLOCK_NO_ID},
      .out_obj_ids = (const uint16_t[]){2}, .en_acc_ids = (const uint16_t[]){1}, .eno_obj_id = 3}));
  if (!b || !signal || !gate || !output || !eno) return;
  vm_block_timer_init_data(vm_block_get_custom_data(b), VM_TIMER_TON, 0, false);
  *(float*)signal->payload = 0.25f;
  *(uint8_t*)gate->payload = 1;
  g_vm_block_fault = false;
  vm_blk_timer(b);
  ck("timer accepts fractional signal and unwired preset", !g_vm_block_fault && *(uint8_t*)output->payload == 1);
  vm_block_get_inputs(b)[1] = bad_acc;
  g_vm_block_fault = false;
  vm_blk_timer(b);
  ck("wired preset error faults", g_vm_block_fault && *(uint8_t*)eno->payload == 0);
  vm_block_get_inputs(b)[0] = bad_acc;
  *(uint8_t*)gate->payload = 0;
  g_vm_block_fault = false;
  vm_blk_timer(b);
  memcpy(&timer, vm_block_get_custom_data(b), sizeof(timer));
  ck("disabled timer resets without resolving signal", !g_vm_block_fault && !timer.elapsed_ms &&
      !(timer.flags & VM_TIMER_F_INITIALIZED) && *(uint8_t*)output->payload == 0);
  vm_block_timer_init_data(vm_block_get_custom_data(b), (vm_timer_mode_e)99, 0, false);
  g_vm_block_fault = false;
  vm_blk_timer(b);
  ck("invalid timer mode faults", g_vm_block_fault && (b->cfg.rt & VM_BLK_RT_CFG_BAD));
  g_vm_block_fault = false;
  vm_blk_timer(b);
  ck("latched config remains a fault", g_vm_block_fault);

  b->cfg.rt = 0;
  vm_edge_init(vm_block_get_custom_data(b), VM_EDGE_BOTH, (vm_edge_val_u){.f = 0});
  vm_block_get_inputs(b)[0] = sig_acc;
  vm_block_get_inputs(b)[1] = NULL;
  *(uint8_t*)gate->payload = 1;
  g_vm_block_fault = false;
  vm_blk_edge(b);
  *(float*)signal->payload = 0.5f;
  vm_blk_edge(b);
  ck("edge publishes pulse", !g_vm_block_fault && *(uint8_t*)output->payload == 1);
  output->head.f.upd = 0;
  vm_blk_edge(b);
  ck("edge clears pulse quietly", *(uint8_t*)output->payload == 0 && !output->head.f.upd);
  vm_block_get_inputs(b)[1] = bad_acc;
  g_vm_block_fault = false;
  vm_blk_edge(b);
  ck("wired threshold error faults", g_vm_block_fault);
  vm_block_get_inputs(b)[0] = bad_acc;
  *(uint8_t*)gate->payload = 0;
  g_vm_block_fault = false;
  vm_blk_edge(b);
  memcpy(&edge, vm_block_get_custom_data(b), sizeof(edge));
  ck("disabled edge resets without resolving signal", !g_vm_block_fault && !(edge.flags & VM_EDGE_F_INITIALIZED));
  vm_edge_init(vm_block_get_custom_data(b), (vm_edge_type_e)99, (vm_edge_val_u){0});
  vm_blk_edge(b);
  ck("invalid edge mode faults", g_vm_block_fault);

  vm_for_code_t loop = {.max_turns = 1};
  g_vm_block_fault = false;
  vm_for_bad_loop(b, &loop, 1, VM_FOR_BAD_CAPPED);
  ck("loop cap latches fault", g_vm_block_fault);
  g_vm_block_fault = false;
  vm_for_bad_loop(b, &loop, 1, VM_FOR_BAD_CAPPED);
  ck("repeated loop cap still faults", g_vm_block_fault);
  g_vm_block_fault = false;
  vm_store_reset();
}
