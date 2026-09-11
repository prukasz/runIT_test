#include "selftest_harness.h"
#include "vm_block.h"
#include "vm_block_build.h"
#include "vm_blocks.h"
#include "vm_block_branch.h"
#include "vm_block_clone.h"
#include "vm_block_expr.h"
#include "vm_block_for.h"
#include "vm_block_set.h"
#include "vm_event.h"
#include "vm_exec.h"
#include "vm_loader.h"
#include "dec_vm_loader.h"
#include "sys_interface.h"
#include "vm_sub.h"
#include "vm_override.h"

// registry id == block_idx == position in the execution order, so an id reads
// as "block N" in an assertion and is also where the walk finds it
#define EX_TICK 0
#define EX_GATED 1
#define EX_TRIG 2
#define EX_SPAN 3
#define EX_INNER 4
#define EX_FAULT 5

// objects
#define EX_O_GATE 0   // the enable source; upd_resetable, so the sweep is visible
#define EX_O_TRIG 1   // the trigger source; likewise
#define EX_O_TICK 2   // EX_TICK's counter
#define EX_O_HELD 3   // what EX_TRIG published, and holds when nothing arrives
#define EX_O_INNER 4  // EX_INNER's counter -- turns of the span, not passes
#define EX_O_KEPT 5   // what EX_FAULT published before it failed
#define EX_O_DIV 6    // EX_FAULT's divisor, taken to zero on pass 2
#define EX_O_BR0 7    // EX_GATED's two branches
#define EX_O_BR1 8
#define EX_ENO_GATED 9
#define EX_ENO_TRIG 10
#define EX_ENO_SPAN 11
#define EX_ENO_FAULT 12

static vm_accessor_t* ex_acc(uint16_t acc_id, uint16_t root_obj) {
  vm_accessor_t* a = NULL;
  if (vm_accessor_create(&a, acc_id, root_obj, 0) != NULL) return NULL;
  (void)vm_accessor_cache_build(a);
  return a;
}

/* out = out + 1, wired to read the object it writes. `acc` must be an accessor
   rooted at `obj` -- the self-reference is the whole mechanism. Priming `upd`
   is what makes the first pass trigger; nothing clears it afterwards, because
   an object built by mk() is not upd_resetable. */
static const uint8_t c_inc[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_ADD};

static bool ex_counter(uint16_t id, uint16_t acc, uint16_t obj) {
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_EXPR,
                                              .in_cnt = 1,
                                              .q_cnt = 1,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = (uint16_t)vm_expr_size(1, sizeof(c_inc)),
                                              .in_acc_ids = (const uint16_t[]){acc},
                                              .out_obj_ids = (const uint16_t[]){obj},
                                              .eno_obj_id = VM_BLOCK_NO_ID});
  if (e != NULL || b == NULL) return false;

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  c->const_cnt = 1;
  c->code_len = sizeof(c_inc);
  c->consts[0].f = 1.0f;
  memcpy(&c->consts[1], c_inc, sizeof(c_inc));

  vm_obj_h o = vm_obj_get_by_id(obj);
  if (!o) return false;
  o->head.f.upd = 1;
  return true;
}

// how many times the supervisor called the counter writing `obj`
static uint32_t ex_count(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o ? (uint32_t)*(float*)o->payload : 0xFFFFFFFFu;
}

static void ex_count_reset(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  if (o) *(float*)o->payload = 0.0f;
}

static bool ex_b(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o && *(uint8_t*)o->payload != 0;
}

static uint8_t ex_upd(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o ? o->head.f.upd : 0xFFu;
}

static float ex_f(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o ? *(float*)o->payload : -1.0f;
}

/* One EXPR of the caller's choosing -- inputs, output object, ENO and code all
   given. Stage T has a builder of its own that fixes the output and ENO by
   convention; the exec stages need to point one anywhere, which is what makes
   an accumulator and a counter expressible at all. */
static bool ex_expr(uint16_t id, const uint16_t* ins, uint8_t in_cnt, uint16_t out_obj, uint16_t eno,
                    const uint32_t* ks, uint8_t k_cnt, const uint8_t* code, uint16_t code_len) {
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_EXPR,
                                              .in_cnt = in_cnt,
                                              .q_cnt = 1,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = (uint16_t)vm_expr_size(k_cnt, code_len),
                                              .in_acc_ids = ins,
                                              .out_obj_ids = (const uint16_t[]){out_obj},
                                              .eno_obj_id = eno});
  if (e != NULL || b == NULL) return false;

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  c->const_cnt = k_cnt;
  c->code_len = code_len;
  for (uint8_t i = 0; i < k_cnt; i++) c->consts[i].u = ks[i];
  memcpy(&c->consts[k_cnt], code, code_len);
  return true;
}

/* The loop itself, spelled the way the editor would show it:
   for (i = start; i <cmp> end; i = i <op> step), and a turn budget. */
typedef struct {
  float start, end, step;
  uint16_t budget;
  uint8_t op, cmp;
} for_loop_t;

/* A FOR owning [span_start, span_end). `custom_len` is the program's word, so
   it is a parameter rather than a sizeof: stage V needs a FOR whose span fits
   and whose loop does not. */
static bool ex_for(uint16_t id, uint16_t span_start, uint16_t span_end, for_loop_t lp, const uint16_t* ins,
                   uint8_t in_cnt, const uint16_t* outs, uint8_t q_cnt, const uint16_t* ens, uint8_t en_cnt,
                   uint16_t eno, uint16_t custom_len) {
  vm_for_code_t for_code = {
      .span = {.start = span_start, .end = span_end},
      .k_start = lp.start,
      .k_end = lp.end,
      .k_step = lp.step,
      .max_turns = lp.budget,
      .op = lp.op,
      .cmp = lp.cmp,
  };
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_FOR,
                                              .in_cnt = in_cnt,
                                              .q_cnt = q_cnt,
                                              .en_cnt = en_cnt,
                                              .en_mode = VM_BLK_EN_ANY,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = custom_len,
                                              .custom_data = (custom_len >= sizeof(vm_for_code_t)) ? &for_code : NULL,
                                              .in_acc_ids = ins,
                                              .out_obj_ids = outs,
                                              .en_acc_ids = ens,
                                              .eno_obj_id = eno});
  return (e == NULL && b != NULL);
}

static bool s_step_test_done;

static void ex_pause_at_sample(void) {
  (void)vm_exec_control(VM_EXEC_PAUSE);
}

static void ex_step_worker(void* arg) {
  (void)arg;
  vm_exec_pass();
  __atomic_store_n(&s_step_test_done, true, __ATOMIC_RELEASE);
  vTaskDelete(NULL);
}

static bool ex_wait_block(uint16_t id) {
  uint64_t until = vm_clock_us() + 2000000;
  do {
    vm_exec_status_t st = vm_exec_status();
    if (st.waiting && st.mode == VM_RUN_BLOCK && st.next_block == id) return true;
    vTaskDelay(1);
  } while (vm_clock_us() < until);
  return false;
}

static bool ex_wait_done(void) {
  uint64_t until = vm_clock_us() + 2000000;
  while (!__atomic_load_n(&s_step_test_done, __ATOMIC_ACQUIRE)) {
    if (vm_clock_us() >= until) return false;
    vTaskDelay(1);
  }
  return true;
}

static void ex_test_controls(void) {
  ck("empty control packet rejected", dec_vm_loader_decode((const uint8_t[]){0x48}, 1) != NULL);
  ck("trailing control payload rejected", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_NORMAL_MODE, 0}, 3) != NULL);
  ck("unknown wire command rejected", dec_vm_loader_decode((const uint8_t[]){0x48, 255}, 2) != NULL);
  ck("wire selects block mode", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_BLOCK_MODE}, 2) == NULL && vm_exec_mode() == VM_RUN_BLOCK);
  ck("wire queues next", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_NEXT}, 2) == NULL && vm_exec_mode() == VM_RUN_BLOCK_STEP);
  ck("wire rewind cancels pending next", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_RESET_TO_START}, 2) == NULL && vm_exec_mode() == VM_RUN_BLOCK);
  ck("wire scan mode holds", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_SCAN_MODE}, 2) == NULL);
  uint32_t passes = vm_exec_pass_count();
  vm_exec_pass();
  ck("scan mode waits for once", vm_exec_pass_count() == passes);
  ck("next rejected in scan mode", vm_exec_control(VM_EXEC_NEXT) != NULL);
  ck("scan once accepted", vm_exec_control(VM_EXEC_ONCE) == NULL);
  ck("duplicate pending once rejected", vm_exec_control(VM_EXEC_ONCE) != NULL);
  vm_exec_pass();
  ck("scan once completed", vm_exec_pass_count() == passes + 1);
  ck("resume after once keeps scan mode held", vm_exec_control(VM_EXEC_RESUME) == NULL && vm_exec_mode() == VM_RUN_SCAN);
  vm_exec_set_sample_hook(ex_pause_at_sample);
  ck("wire scan once accepted", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_ONCE}, 2) == NULL);
  vm_exec_pass();
  vm_exec_set_sample_hook(NULL);
  ck("pause during scan completion does not replay once", vm_exec_control(VM_EXEC_RESUME) == NULL && vm_exec_mode() == VM_RUN_SCAN);

  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    ex_count_reset(EX_O_TICK);
    ex_count_reset(EX_O_INNER);
    passes = vm_exec_pass_count();
    ck("block mode selected", vm_exec_control(VM_EXEC_BLOCK_MODE) == NULL);
    ck("once rejected in block mode", vm_exec_control(VM_EXEC_ONCE) != NULL);
    vm_exec_pass();
    ck("block mode holds before scan", ex_count(EX_O_TICK) == 0 && vm_exec_pass_count() == passes);
    ck("first next accepted", vm_exec_control(VM_EXEC_NEXT) == NULL);
    ck("duplicate pending next rejected", vm_exec_control(VM_EXEC_NEXT) != NULL);
    __atomic_store_n(&s_step_test_done, false, __ATOMIC_RELEASE);
    bool started = xTaskCreate(ex_step_worker, "vm_step_test", 4096, NULL, 5, NULL) == pdPASS;
    ck("step worker started", started);
    if (!started) { vm_exec_stop(); return; }
    bool held = ex_wait_block(EX_GATED);
    ck("one next dispatches only block zero", held && ex_count(EX_O_TICK) == 1 && ex_count(EX_O_INNER) == 0);
    const uint16_t next[] = {EX_TRIG, EX_SPAN, EX_INNER};
    for (unsigned i = 0; i < 3 && held; ++i) {
      ck("next accepted", vm_exec_control(VM_EXEC_NEXT) == NULL);
      held = ex_wait_block(next[i]);
      ck("next stops before following dispatch", held);
    }
    if (held) {
      ck("FOR entry does not run its body", ex_count(EX_O_INNER) == 0);
      uint64_t stamp = vm_now_ms();
      uint8_t events = vm_event_count();
      cb_event_t pending = {0};
      ck("event queues while stepping", vm_event_post(&pending));
      ck("pause nested body", vm_exec_control(VM_EXEC_PAUSE) == NULL);
      ck("resume preserves block mode", vm_exec_control(VM_EXEC_RESUME) == NULL && vm_exec_mode() == VM_RUN_BLOCK);
      ck("next enters first loop iteration", vm_exec_control(VM_EXEC_NEXT) == NULL);
      held = ex_wait_block(EX_INNER);
      ck("repeated body is a separate step", held && ex_count(EX_O_INNER) == 1 && vm_now_ms() == stamp);
      ck("step preserves event snapshot", held && vm_event_count() == events);
    }
    if (held && attempt == 0) {
      ck("next enters second iteration", vm_exec_control(VM_EXEC_NEXT) == NULL);
      held = ex_wait_block(EX_FAULT);
      ck("FOR returns without an extra step", held && ex_count(EX_O_INNER) == 2);
      ck("final block step accepted", vm_exec_control(VM_EXEC_NEXT) == NULL);
      ck("last step completes scan", ex_wait_done() && vm_exec_pass_count() == passes + 1);
      ck("pass duration includes operator waits", vm_exec_last_pass_us() > 0);
    } else if (attempt < 2) {
      ck("rewind releases nested stack", vm_exec_control(VM_EXEC_RESET_TO_START) == NULL && ex_wait_done());
      ck("rewind preserves values and excludes cancelled scan", ex_count(EX_O_INNER) == 1 && vm_exec_pass_count() == passes);
      ck("rewind holds block mode at start", vm_exec_mode() == VM_RUN_BLOCK && !vm_exec_status().scan_active);
    } else {
      ck("wire reset releases a nested held scan", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_RESET}, 2) == NULL && ex_wait_done());
      ck("nested reset unloads safely", vm_exec_mode() == VM_RUN_STOPPED && !vm_block_get_by_id(0) && vm_exec_pass_count() == 0);
    }
    // A failed wait must still release the worker before the next fixture.
    (void)vm_exec_control(VM_EXEC_RESET_TO_START);
    ck("worker released", ex_wait_done());
  }
  ck("wire normal mode runs", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_NORMAL_MODE}, 2) == NULL && vm_exec_mode() == VM_RUN_RUNNING);
  ck("wire pause normal", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_PAUSE}, 2) == NULL && vm_exec_mode() == VM_RUN_FROZEN);
  ck("wire resume normal", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_RESUME}, 2) == NULL && vm_exec_mode() == VM_RUN_RUNNING);
  ck("rewind normal holds", vm_exec_control(VM_EXEC_RESET_TO_START) == NULL && vm_exec_mode() == VM_RUN_FROZEN);
  ck("resume rewind restores normal", vm_exec_control(VM_EXEC_RESUME) == NULL && vm_exec_mode() == VM_RUN_RUNNING);
  ck("invalid command rejected", vm_exec_control((vm_exec_command_e)255) != NULL);
  vm_exec_stop();
  ck("wire reset unloads program", dec_vm_loader_decode((const uint8_t[]){0x48, VM_EXEC_RESET}, 2) == NULL &&
     vm_loader_state() == VM_LOAD_EMPTY && !vm_block_get_by_id(0) && vm_exec_pass_count() == 0);
  ck("open empty stepping fixture", vm_loader_open(0, 0, 0, 1024) == NULL);
  (void)vm_exec_control(VM_EXEC_BLOCK_MODE);
  (void)vm_exec_control(VM_EXEC_NEXT);
  vm_exec_set_sample_hook(ex_pause_at_sample);
  vm_exec_pass();
  vm_exec_set_sample_hook(NULL);
  ck("empty scan consumes next even if paused during completion", vm_exec_pass_count() == 1 &&
     vm_exec_control(VM_EXEC_RESUME) == NULL && vm_exec_mode() == VM_RUN_BLOCK);
  vm_loader_reset();
}

void test_exec_pass(void) {
  ESP_LOGI(TAG, "-- R: the pass --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 32, [VM_REG_ACC] = 24, [VM_REG_BLK] = 6};
  ck("execution arena opens", vm_store_open(DIRECT_POOL, counts) == NULL);

  /* The gate and the trigger are upd_resetable, because the end-of-pass sweep
     is one of the things under test and a non-resettable object is deliberately
     exempt from it. Nothing else is, so what one pass wrote is still standing
     when the next starts -- which is what the counters rely on and what the
     "outputs stand" assertions are about. */
  vm_obj_head_t gh = hd(VM_OBJ_B, 1);
  gh.f.mutable = 1;
  gh.f.upd_resetable = 1;
  vm_obj_h gate = NULL, trig = NULL;
  bool built = vm_obj_create(&gate, EX_O_GATE, &gh, NULL) == NULL;
  vm_obj_head_t th = hd(VM_OBJ_U32, 1);
  th.f.mutable = 1;
  th.f.upd_resetable = 1;
  built = built && vm_obj_create(&trig, EX_O_TRIG, &th, NULL) == NULL;

  built = built && mk(EX_O_TICK, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(EX_O_HELD, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(EX_O_INNER, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(EX_O_KEPT, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(EX_O_DIV, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(EX_O_BR0, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(EX_O_BR1, VM_OBJ_B, 1, NULL, true) != NULL;
  for (uint16_t i = EX_ENO_GATED; i <= EX_ENO_FAULT; i++) built = built && mk(i, VM_OBJ_B, 1, NULL, true) != NULL;
  ck("objects built", built);

  // 0 gate, 1 trig, 2 the tick counter's own output, 3 the inner one's, 4 the divisor
  bool accs = true;
  accs = accs && ex_acc(0, EX_O_GATE) != NULL;
  accs = accs && ex_acc(1, EX_O_TRIG) != NULL;
  accs = accs && ex_acc(2, EX_O_TICK) != NULL;
  accs = accs && ex_acc(3, EX_O_INNER) != NULL;
  accs = accs && ex_acc(4, EX_O_DIV) != NULL;
  ck("accessors built", accs);

  *(float*)vm_obj_get_by_id(EX_O_DIV)->payload = 2.0f;
  vm_obj_get_by_id(EX_O_DIV)->head.f.upd = 1;  // never swept, so EX_FAULT runs every pass

  /* 6.0 / divisor -- a program that succeeds on pass 1 and divides by zero on
     pass 2, which is how on_error is exercised now that there is no block
     whose whole job is to fail. */
  static const uint8_t c_div6[] = {VM_EXPR_K, 0, VM_EXPR_IN, 0, VM_EXPR_DIV};
  static const uint8_t c_pass1[] = {VM_EXPR_IN, 0};
  const uint32_t k_six[] = {kf(6.0f)};
  const for_loop_t twice = {.start = 0, .end = 2, .step = 1, .budget = 8, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};

  bool blocks = true;
  blocks = blocks && ex_counter(EX_TICK, 2, EX_O_TICK);
  // enable-driven: the router decides only while its EN says so
  {
    vm_block_h b = NULL;
    err_h e = vm_block_create(&b, EX_GATED,
                              &(vm_block_cfg_t){.block_idx = EX_GATED,
                                                .block_type = VM_BLK_SWITCH,
                                                .in_cnt = 1,
                                                .q_cnt = 2,
                                                .en_cnt = 1,
                                                .en_mode = VM_BLK_EN_ANY,
                                                .on_error = VM_BLK_ERR_STOP,
                                                .in_acc_ids = (const uint16_t[]){1},
                                                .out_obj_ids = (const uint16_t[]){EX_O_BR0, EX_O_BR1},
                                                .en_acc_ids = (const uint16_t[]){0},
                                                .eno_obj_id = EX_ENO_GATED});
    blocks = blocks && e == NULL && b != NULL;
  }
  // update-driven: publishes what arrived, holds it when nothing does
  blocks = blocks && ex_expr(EX_TRIG, (const uint16_t[]){1}, 1, EX_O_HELD, EX_ENO_TRIG, NULL, 0, c_pass1, sizeof(c_pass1));
  // the span owner, claiming exactly the block that follows it
  blocks = blocks && ex_for(EX_SPAN, EX_INNER, EX_INNER + 1, twice, NULL, 0, NULL, 0, NULL, 0, EX_ENO_SPAN,
                            sizeof(vm_for_code_t));
  blocks = blocks && ex_counter(EX_INNER, 3, EX_O_INNER);
  blocks = blocks && ex_expr(EX_FAULT, (const uint16_t[]){4}, 1, EX_O_KEPT, EX_ENO_FAULT, k_six, 1, c_div6, sizeof(c_div6));
  ck("blocks built in execution order", blocks);
  if (!blocks) return;

  /* ---- pass 1: gate open, trigger fresh, divisor sound ---- */
  *(uint8_t*)gate->payload = 1;
  gate->head.f.upd = 1;
  *(uint32_t*)trig->payload = 1u;
  trig->head.f.upd = 1;

  vm_exec_reset_stats();
  vm_exec_pass();

  ck("a pass counts", vm_exec_pass_count() == 1);
  ck("an ungated block was called and acted", ex_count(EX_O_TICK) == 1);
  ck("an enabled router decided", ex_b(EX_O_BR1) && !ex_b(EX_O_BR0) && ex_b(EX_ENO_GATED));
  ck("a block asking what arrived acted on a fresh input", near_f(ex_f(EX_O_HELD), 1.0f) && ex_b(EX_ENO_TRIG));
  ck("...and latched that it was triggered", (vm_block_get_by_id(EX_TRIG)->cfg.rt & VM_BLK_RT_TRIGGERED) != 0);

  /* Twice, not three times. Three would mean the outer walk ran the body as
     well as the owner running it -- which is the whole reason a claim exists. */
  ck("a span owner ran its range, and only it did", ex_count(EX_O_INNER) == 2 && ex_b(EX_ENO_SPAN));

  ck("a block that succeeded published and kept its true ENO", near_f(ex_f(EX_O_KEPT), 3.0f) && ex_b(EX_ENO_FAULT));

  /* ---- the end-of-pass sweep ---- */
  ck("upd is cleared at the end of the pass", gate->head.f.upd == 0 && trig->head.f.upd == 0);
  ck("...and a non-resetable object is left alone", ex_upd(EX_O_TICK) == 1);

  /* ---- pass 2: gate shut, trigger stale, divisor zero ----
     These outputs are not upd_resetable, so the end-of-pass sweep left their
     `upd` standing from when they were written. Cleared by hand here, because
     what the next pass has to show is that nothing *sets* it -- a stand-down
     that is loud would read as an arrival to everything below. */
  vm_obj_get_by_id(EX_O_HELD)->head.f.upd = 0;
  vm_obj_get_by_id(EX_ENO_TRIG)->head.f.upd = 0;
  vm_obj_get_by_id(EX_ENO_FAULT)->head.f.upd = 0;
  vm_obj_get_by_id(EX_O_BR1)->head.f.upd = 0;
  *(uint8_t*)gate->payload = 0;
  *(float*)vm_obj_get_by_id(EX_O_DIV)->payload = 0.0f;

  vm_exec_pass();

  /* Every block is *called* every pass -- that is the whole of the
     supervisor's policy. What changes is whether the block acts. */
  ck("every block is called again regardless", ex_count(EX_O_TICK) == 2 && ex_count(EX_O_INNER) == 4);

  /* Standing down keeps the data and withdraws the flow. The output holds
     last pass's answer -- nothing changed, so it is still the right one --
     while ENO drops so everything below stands down in turn. */
  ck("a stale trigger means the block does not act", !ex_b(EX_ENO_TRIG));
  ck("...and it holds what it last published", near_f(ex_f(EX_O_HELD), 1.0f));
  ck("...having left it alone entirely, upd included", ex_upd(EX_O_HELD) == 0);
  ck("...with the ENO taken false quietly", ex_upd(EX_ENO_TRIG) == 0);

  /* A router is the one shape that does *not* leave its outputs standing, and
     this is why: they feed enable lists, so a branch left high behind a closed
     gate is a whole subtree still running. Quietly, though -- stage U is where
     that rule is actually pinned down. */
  ck("a shut gate means the router decides nothing", !ex_b(EX_O_BR0) && !ex_b(EX_O_BR1) && !ex_b(EX_ENO_GATED));
  ck("...quietly, so nothing below reads it as an arrival", ex_upd(EX_O_BR1) == 0);

  /* on_error STOP: the block published a true value and a true ENO last pass,
     and failed this one. It is the *flow* that stops -- what was published
     stands, exactly as it does for a block that simply chose not to act, and
     the false ENO is what keeps anything below from acting on it. */
  ck("on_error STOP publishes a false ENO", !ex_b(EX_ENO_FAULT));
  ck("...and leaves what was published standing", near_f(ex_f(EX_O_KEPT), 3.0f));
  ck("...taken false quietly, like every other stand-down", ex_upd(EX_ENO_FAULT) == 0);

  /* That the pass timer records something, and nothing about how long.
     This stage runs a block that deliberately fails, and reporting that
     failure goes out over UART from the error handler -- which can preempt
     mid-pass and costs milliseconds per line, dwarfing the six blocks it is
     supposedly timing. The 10 ms floor is a property of the supervisor loop
     (one tick per pass), not of a pass measured with a logger in it; vm_bench.c
     is where per-access cost is actually measured. */
  ESP_LOGI(TAG, "  last pass: %lu us over 6 blocks", (unsigned long)vm_exec_last_pass_us());

  uint32_t passes = vm_exec_pass_count();
  uint64_t stamp = vm_now_ms();
  cb_event_t pending = {0};
  vm_event_reset();
  ck("event queues before freezing", vm_event_post(&pending));
  vm_exec_set_mode(VM_RUN_FROZEN);
  vm_exec_pass();
  ck("a frozen pass does not start or latch time", vm_exec_pass_count() == passes && vm_now_ms() == stamp);
  ck("a frozen pass leaves pending events queued", vm_event_count() == 0);
  vm_exec_set_mode(VM_RUN_STEP);
  vm_exec_pass();
  ck("step completes one pass and freezes", vm_exec_pass_count() == passes + 1 && vm_exec_mode() == VM_RUN_FROZEN);
  ck("step latches the pending event", vm_event_count() == 1);
  vm_exec_pass();
  ck("step does not admit the next pass", vm_exec_pass_count() == passes + 1);
  vm_exec_set_mode(VM_RUN_STOPPED);

  vm_for_code_t* loop = (vm_for_code_t*)vm_block_get_custom_data(vm_block_get_by_id(EX_SPAN));
  loop->span = (vm_span_t){EX_SPAN, EX_INNER + 1};
  ex_count_reset(EX_O_INNER);
  vm_exec_pass();
  ck("self-containing span cannot recurse; body only runs inline", ex_count(EX_O_INNER) == 1 && !ex_b(EX_ENO_SPAN));
  loop->span = (vm_span_t){EX_INNER, EX_INNER + 1};
  ex_test_controls();
}

/* ==========================================================================
   S -- events

   Activation is transient, data is persistent. This is the transient half, and
   it is transient in the strongest sense available: an event lives exactly one
   scan cycle. Two buffers, swapped once per pass; whatever a pass does not take
   is gone at the next swap, on purpose.
   ========================================================================== */

// what sys_callbacks would route: an IO edge, built by hand
static cb_event_t io_evt(uint8_t device, uint8_t pin, int32_t value) {
  cb_event_t e = {0};
  e.head.callback_type = CALLBACK_IO;
  e.head.route_mask = SYS_CB_ROUTE_BIT(SYS_CB_ROUTE_VM);
  e.event.io.device_id = device;
  e.event.io.pin_id = pin;
  e.event.io.trigger_value = value;
  return e;
}

void test_events(void) {
  ESP_LOGI(TAG, "-- S: events --");
  vm_event_reset();

  ck("an empty cycle has nothing to look at", vm_event_count() == 0 && vm_event_at(0) == NULL);

  cb_event_t a = io_evt(1, 4, 11);
  cb_event_t b = io_evt(1, 5, 22);
  cb_event_t c = io_evt(2, 4, 33);
  ck("posting takes", vm_event_post(&a) && vm_event_post(&b) && vm_event_post(&c));

  /* A post lands in the buffer the *next* pass reads, never the one a pass is
     reading. That is what lets a whole pass traverse one snapshot with no lock
     on the reading side at all. */
  ck("a post is not visible in the cycle it arrived", vm_event_count() == 0);

  vm_event_drain();
  ck("the swap hands the whole batch to this cycle", vm_event_count() == 3);

  /* The whole query API: walk them and recognise what you care about. The
     system's own struct arrives intact -- nothing was translated on the way
     in, so a block reads the same fields a driver wrote. */
  const cb_event_t* e0 = vm_event_at(0);
  ck("the callback arrives intact, head and all", e0 && e0->head.callback_type == CALLBACK_IO && e0->event.io.pin_id == 4 && e0->event.io.trigger_value == 11);
  ck("...in the order it was routed", vm_event_at(1)->event.io.pin_id == 5 && vm_event_at(2)->event.io.device_id == 2);
  ck("past the end reads NULL", vm_event_at(3) == NULL);

  /* Nothing is consumed, which is what lets two blocks watch the same pin:
     there is no ownership to arbitrate and no order to get wrong. */
  ck("reading does not consume", vm_event_count() == 3 && vm_event_at(0)->event.io.trigger_value == 11);

  /* The whole point of the design: a cycle's events are dropped at the next
     swap whether anything acted on them or not. A block that was disabled
     when the edge arrived simply misses it. */
  vm_event_drain();
  ck("an unhandled event is gone one cycle later", vm_event_count() == 0);

  /* Overflow drops and raises a system error -- never silent, never unbounded.
     The error is built at task level because the post path may be an ISR.
     Note this is *overflow* only: events dropped by the swap for want of a
     taker are normal, and silent. */
  vm_event_reset();
  uint16_t stored = 0;
  for (uint16_t i = 0; i < VM_EVENT_DEPTH + 4; i++) {
    cb_event_t e = io_evt(1, (uint8_t)i, i);
    if (vm_event_post(&e)) stored++;
  }
  ck("a cycle fills to exactly its depth", stored == VM_EVENT_DEPTH);
  // discarded rather than pushed, like every other expected rejection in this
  // file -- the point is that one was built, not what it prints
  ck("overflow raises an error naming the drops", vm_event_take_overflow() != NULL);
  ck("taking the overflow clears it", vm_event_take_overflow() == NULL);

  vm_event_drain();
  ck("everything that was accepted is there", vm_event_count() == VM_EVENT_DEPTH);
  ck("and it kept its order", vm_event_at(0)->event.io.pin_id == 0);

  vm_event_reset();
  ck("reset empties both buffers", vm_event_count() == 0 && vm_event_at(0) == NULL);
}

/* ==========================================================================
   T -- the expression blocks

   The first real blocks in the palette, and the first whose behaviour is
   *compiled* rather than wired: an RPN instruction stream living in the
   block's own custom_data. So this stage is as much about the bytecode as
   about the block. Every program below is hand-assembled here and run through
   vm_exec_pass(), which is the same path a client-compiled one takes -- the
   block is dispatched from the real palette and there is no test-only seam.

   Registry id == block_idx == position in the execution order, as in stage R,
   and each block gets its own result and ENO object so no assertion about one
   can be satisfied by another.
   ========================================================================== */

#define EXPR_OUT(n) ((uint16_t)(10 + (n)))  // block n's result object
#define EXPR_ENO(n) ((uint16_t)(30 + (n)))  // block n's ENO

static float out_f(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(EXPR_OUT(n));
  return o ? *(float*)o->payload : -1.0f;
}

static uint32_t out_u(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(EXPR_OUT(n));
  return o ? *(uint32_t*)o->payload : 0xDEADBEEFu;
}

static bool eno_of(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(EXPR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

// the block's own view of its fault episode -- VM_EXPR_RT_FAULTED
static uint8_t expr_rt(uint16_t n) {
  vm_block_h b = vm_block_get_by_id(n);
  return b ? ((const vm_expr_code_t*)vm_block_get_custom_data(b))->rt : 0xFFu;
}

static bool cfg_bad(uint16_t n) {
  vm_block_h b = vm_block_get_by_id(n);
  return b && (b->cfg.rt & VM_BLK_RT_CFG_BAD) != 0;
}

/* Builds one expression block and lays its code out exactly as the 0x45
   record's private-state tail would: header, then the literals, then the
   bytecode. `custom_len` is the program's word, so it comes from
   vm_expr_size() rather than from a struct the loader knows about. */
static bool expr_block(uint16_t id, uint8_t type, const uint16_t* ins, uint8_t in_cnt, const uint32_t* ks,
                       uint8_t k_cnt, const uint8_t* code, uint16_t code_len, uint16_t custom_len) {
  const uint16_t outs[1] = {EXPR_OUT(id)};
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = type,
                                              .in_cnt = in_cnt,
                                              .q_cnt = 1,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = custom_len,
                                              .in_acc_ids = ins,
                                              .out_obj_ids = outs,
                                              .eno_obj_id = EXPR_ENO(id)});
  if (e != NULL || b == NULL) return false;
  if (custom_len < vm_expr_size(k_cnt, code_len)) return true;  // the deliberately-too-short case

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  c->const_cnt = k_cnt;
  c->code_len = code_len;
  for (uint8_t i = 0; i < k_cnt; i++) c->consts[i].u = ks[i];
  memcpy(&c->consts[k_cnt], code, code_len);
  return true;
}

// the common case: custom_data sized to exactly what the program needs
static bool expr_blk(uint16_t id, uint8_t type, const uint16_t* ins, uint8_t in_cnt, const uint32_t* ks, uint8_t k_cnt,
                     const uint8_t* code, uint16_t code_len) {
  return expr_block(id, type, ins, in_cnt, ks, k_cnt, code, code_len, (uint16_t)vm_expr_size(k_cnt, code_len));
}

void test_expr(void) {
  ESP_LOGI(TAG, "-- T: expression blocks --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 48, [VM_REG_ACC] = 8, [VM_REG_BLK] = 16};
  (void)vm_store_open(DIRECT_POOL, counts);

  /* Inputs 0..4 are deliberately *not* upd_resetable, so their `upd` survives
     the end-of-pass sweep and every pass triggers -- the same standing
     freshness a constant has. Input 5 is resetable, which is what makes the
     stand-down case at the end possible. */
  bool built = true;
  for (uint16_t i = 0; i < 5; i++) {
    built = built && mk(i, i < 3 ? VM_OBJ_F : VM_OBJ_U32, 1, NULL, true) != NULL;
    if (built) vm_obj_get_by_id(i)->head.f.upd = 1;
  }
  vm_obj_head_t sh = hd(VM_OBJ_F, 1);
  sh.f.mutable = 1;
  sh.f.upd_resetable = 1;
  vm_obj_h stale_in = NULL;
  built = built && vm_obj_create(&stale_in, 5, &sh, NULL) == NULL;

  *(float*)vm_obj_get_by_id(0)->payload = 3.0f;   // A
  *(float*)vm_obj_get_by_id(1)->payload = 4.0f;   // B
  *(float*)vm_obj_get_by_id(2)->payload = 0.0f;   // the divisor, zero to begin with
  *(uint32_t*)vm_obj_get_by_id(3)->payload = 0x12345678u;
  *(uint32_t*)vm_obj_get_by_id(4)->payload = 8u;
  *(float*)stale_in->payload = 5.0f;
  stale_in->head.f.upd = 1;

  // results 10..24 and ENOs 30..44, one pair per block
  for (uint16_t n = 0; n <= 14; n++) {
    bool bits = (n >= 9 && n <= 12);
    built = built && mk(EXPR_OUT(n), bits ? VM_OBJ_U32 : VM_OBJ_F, 1, NULL, true) != NULL;
    built = built && mk(EXPR_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  }
  ck("objects built", built);

  bool accs = true;
  for (uint16_t i = 0; i < 6; i++) accs = accs && ex_acc(i, i) != NULL;
  ck("accessors built", accs);

  /* ---- the programs, in execution order ---------------------------------
     Written as a client would emit them: RPN, one opcode byte, an operand
     byte after IN and K only. */
  static const uint8_t c_add[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_ADD};
  // (A + B) * 2 - hypot(A, B)  ->  14 - 5
  static const uint8_t c_compound[] = {VM_EXPR_IN, 0, VM_EXPR_IN,  1, VM_EXPR_ADD, VM_EXPR_K,     0,
                                       VM_EXPR_MUL, VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_HYPOT, VM_EXPR_SUB};
  // A < B ? 10 : 20 -- both arms evaluated, which is what having no jump means
  static const uint8_t c_sel[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_LT, VM_EXPR_K, 0, VM_EXPR_K, 1, VM_EXPR_SEL};
  static const uint8_t c_reuse[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 0, VM_EXPR_MUL};  // one pin, twice
  static const uint8_t c_div[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_DIV};
  static const uint8_t c_badop[] = {VM_EXPR_IN, 0, 0xFE};
  static const uint8_t c_under[] = {VM_EXPR_IN, 0, VM_EXPR_ADD};   // ADD with one operand
  static const uint8_t c_two[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1};   // ends holding two values
  static const uint8_t c_lazy[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_MUL};  // pin 1 declared, never named
  static const uint8_t c_shr[] = {VM_BIT_IN, 0, VM_BIT_IN, 1, VM_BIT_SHR, VM_BIT_K, 0, VM_BIT_AND};
  static const uint8_t c_pop[] = {VM_BIT_IN, 0, VM_BIT_NOT, VM_BIT_POPCNT};
  static const uint8_t c_shl32[] = {VM_BIT_IN, 0, VM_BIT_K, 0, VM_BIT_SHL};
  static const uint8_t c_rol[] = {VM_BIT_IN, 0, VM_BIT_K, 0, VM_BIT_ROL};
  static const uint8_t c_pass[] = {VM_EXPR_IN, 0};

  const uint32_t k_two[] = {kf(2.0f)};
  const uint32_t k_arms[] = {kf(10.0f), kf(20.0f)};
  const uint32_t k_ff[] = {0xFFu};
  const uint32_t k_32[] = {32u};
  const uint32_t k_8[] = {8u};

  bool blk = true;
  blk = blk && expr_blk(0, VM_BLK_EXPR, (uint16_t[]){0, 1}, 2, NULL, 0, c_add, sizeof(c_add));
  blk = blk && expr_blk(1, VM_BLK_EXPR, (uint16_t[]){0, 1}, 2, k_two, 1, c_compound, sizeof(c_compound));
  blk = blk && expr_blk(2, VM_BLK_EXPR, (uint16_t[]){0, 1}, 2, k_arms, 2, c_sel, sizeof(c_sel));
  blk = blk && expr_blk(3, VM_BLK_EXPR, (uint16_t[]){0}, 1, NULL, 0, c_reuse, sizeof(c_reuse));
  blk = blk && expr_blk(4, VM_BLK_EXPR, (uint16_t[]){0, 2}, 2, NULL, 0, c_div, sizeof(c_div));
  blk = blk && expr_blk(5, VM_BLK_EXPR, (uint16_t[]){0}, 1, NULL, 0, c_badop, sizeof(c_badop));
  blk = blk && expr_blk(6, VM_BLK_EXPR, (uint16_t[]){0}, 1, NULL, 0, c_under, sizeof(c_under));
  blk = blk && expr_blk(7, VM_BLK_EXPR, (uint16_t[]){0, 1}, 2, NULL, 0, c_two, sizeof(c_two));
  blk = blk && expr_blk(8, VM_BLK_EXPR, (uint16_t[]){0, VM_BLOCK_NO_ID}, 2, k_two, 1, c_lazy, sizeof(c_lazy));
  blk = blk && expr_blk(9, VM_BLK_EXPR_BIT, (uint16_t[]){3, 4}, 2, k_ff, 1, c_shr, sizeof(c_shr));
  blk = blk && expr_blk(10, VM_BLK_EXPR_BIT, (uint16_t[]){3}, 1, NULL, 0, c_pop, sizeof(c_pop));
  blk = blk && expr_blk(11, VM_BLK_EXPR_BIT, (uint16_t[]){3}, 1, k_32, 1, c_shl32, sizeof(c_shl32));
  blk = blk && expr_blk(12, VM_BLK_EXPR_BIT, (uint16_t[]){3}, 1, k_8, 1, c_rol, sizeof(c_rol));
  blk = blk && expr_blk(13, VM_BLK_EXPR, (uint16_t[]){5}, 1, NULL, 0, c_pass, sizeof(c_pass));
  // custom_data too short to hold even the header, let alone what it claims
  blk = blk && expr_block(14, VM_BLK_EXPR, (uint16_t[]){0}, 1, NULL, 0, c_add, sizeof(c_add), 2);
  ck("expression blocks built in execution order", blk);
  if (!blk) return;

  /* ---- pass 1 ---- */
  vm_exec_reset_stats();
  vm_exec_pass();

  ck("A + B", near_f(out_f(0), 7.0f) && eno_of(0));
  ck("(A + B) * K - hypot(A, B) -- literals and a nested call", near_f(out_f(1), 9.0f) && eno_of(1));
  ck("a comparison feeding SEL picks the near arm", near_f(out_f(2), 10.0f) && eno_of(2));
  ck("one pin named twice is one value", near_f(out_f(3), 9.0f) && eno_of(3));

  /* The laziness the input cache buys: pin 1 exists, nothing is wired to it,
     and the code never names it -- so it is never read and never fails. An
     eager prefetch would fault this block every pass. */
  ck("a declared but unnamed pin is never read", near_f(out_f(8), 6.0f) && eno_of(8));

  ck("bitwise: (X >> Y) & 0xFF", out_u(9) == 0x56u && eno_of(9));
  ck("bitwise: popcount of ~X", out_u(10) == 19u && eno_of(10));
  ck("a shift of 32 is 0, not undefined", out_u(11) == 0u && eno_of(11));
  ck("a rotate carries the top byte round", out_u(12) == 0x34567812u && eno_of(12));

  /* Every fault is the same shape: nothing published, and the flow withdrawn
     by the supervisor under cfg.on_error rather than by the block. */
  ck("divide by zero publishes nothing", near_f(out_f(4), 0.0f) && !eno_of(4));
  ck("...and latches the fault episode", (expr_rt(4) & VM_EXPR_RT_FAULTED) != 0);
  ck("an unknown opcode faults", !eno_of(5) && near_f(out_f(5), 0.0f));
  ck("a stack underflow faults", !eno_of(6));
  ck("a program ending on two values faults", !eno_of(7));
  ck("custom_data too short for the header faults", !eno_of(14));

  /* Malformed code is a standing condition, so it is latched and reported once
     per load -- at 100 Hz the alternative is six thousand identical errors a
     minute. Bad *data* is not latched that way; see the re-arm below. */
  ck("a malformed program is latched, so it reports once", cfg_bad(5) && cfg_bad(6) && cfg_bad(7) && cfg_bad(14));
  ck("...and a well-formed one is not", !cfg_bad(0) && !cfg_bad(4) && !cfg_bad(9));

  ck("a fresh input runs the expression", near_f(out_f(13), 5.0f) && eno_of(13));
  ck("the sweep clears a resetable input's upd", stale_in->head.f.upd == 0);

  /* ---- pass 2: the divisor is no longer zero, the passthrough's input is
          no longer fresh ---- */
  *(float*)vm_obj_get_by_id(2)->payload = 2.0f;
  vm_obj_get_by_id(EXPR_ENO(13))->head.f.upd = 0;
  vm_exec_pass();

  ck("the same expression recovers once the data does", near_f(out_f(4), 1.5f) && eno_of(4));
  ck("...and a clean evaluation re-arms fault reporting", (expr_rt(4) & VM_EXPR_RT_FAULTED) == 0);
  ck("a malformed program stays latched across passes", cfg_bad(5));

  /* An expression is a function of its inputs, so nothing arriving means the
     answer cannot have changed: the block stands down, its result stands, and
     only the flow is withdrawn. */
  ck("a stale input means the expression does not run", !eno_of(13));
  ck("...and its result stands", near_f(out_f(13), 5.0f));
  ck("...with the ENO taken false quietly", vm_obj_get_by_id(EXPR_ENO(13))->head.f.upd == 0);
}

/* ==========================================================================
   Stage U -- the flow routers

   IF and SWITCH are the first blocks whose outputs are *flow* rather than data,
   so this stage is mostly about the one rule that follows from it: a router
   that is not deciding drives nothing high. Every way of not deciding gets its
   own block -- disabled, unwired, out of range, wrong shape -- and the one that
   matters most is the gated switch, where a stale branch left standing would
   enable everything below it through a closed gate.

   Registry id == block_idx == position in the execution order, as in stages R
   and T. Every block gets its own branch objects and its own ENO, so nothing
   asserted about one can be satisfied by another.
   ========================================================================== */

#define BR_OUT(n, k) ((uint16_t)(10 + (n) * 4 + (k)))  // block n's branch k
#define BR_ENO(n) ((uint16_t)(40 + (n)))

static const uint8_t s_br_q[7] = {2, 4, 4, 4, 4, 1, 2};  // q_cnt per block, in order

static bool br_out(uint16_t n, uint8_t k) {
  vm_obj_h o = vm_obj_get_by_id(BR_OUT(n, k));
  return o && *(uint8_t*)o->payload != 0;
}

static bool br_eno(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(BR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

static uint8_t br_upd(uint16_t n, uint8_t k) {
  vm_obj_h o = vm_obj_get_by_id(BR_OUT(n, k));
  return o ? o->head.f.upd : 0xFFu;
}

/* The assertion this stage is made of: branch `want` high and every other
   branch of the same block low. `want` < 0 means no branch at all, which is
   what disabled, unwired and out-of-range must all look like from outside. */
static bool one_hot(uint16_t n, int want) {
  for (uint8_t k = 0; k < s_br_q[n]; k++) {
    if (br_out(n, k) != (k == want)) return false;
  }
  return true;
}

/* A router carries no private state, so there is no code to lay out and no
   payload argument -- which is the point, and asserted below. */
static bool br_block(uint16_t id, uint8_t type, uint16_t in_acc, uint16_t en_acc) {
  uint16_t outs[4];
  for (uint8_t k = 0; k < s_br_q[id]; k++) outs[k] = BR_OUT(id, k);
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = type,
                                              .in_cnt = 1,
                                              .q_cnt = s_br_q[id],
                                              .en_cnt = (en_acc == VM_BLOCK_NO_ID) ? 0u : 1u,
                                              .en_mode = VM_BLK_EN_ANY,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = VM_BRANCH_CUSTOM_LEN,
                                              .in_acc_ids = (const uint16_t[]){in_acc},
                                              .out_obj_ids = outs,
                                              .en_acc_ids = (const uint16_t[]){en_acc},
                                              .eno_obj_id = BR_ENO(id)});
  return e == NULL && b != NULL;
}

void test_branch(void) {
  ESP_LOGI(TAG, "-- U: flow routers --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 48, [VM_REG_ACC] = 8, [VM_REG_BLK] = 8};
  (void)vm_store_open(DIRECT_POOL, counts);

  /* 0 condition, 1 selector, 2 gate, 3 a fractional selector, 4 a negative one.
     None is upd_resetable, so the end-of-pass sweep leaves them alone -- these
     blocks never ask about freshness, which is the whole difference between a
     router and an expression. */
  bool built = true;
  built = built && mk(0, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(1, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(2, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(3, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(4, VM_OBJ_F, 1, NULL, true) != NULL;

  for (uint16_t n = 0; n < 7; n++) {
    for (uint8_t k = 0; k < s_br_q[n]; k++) built = built && mk(BR_OUT(n, k), VM_OBJ_B, 1, NULL, true) != NULL;
    built = built && mk(BR_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  }
  ck("objects built", built);

  bool accs = true;
  for (uint16_t i = 0; i < 5; i++) accs = accs && ex_acc(i, i) != NULL;
  ck("accessors built", accs);

  *(float*)vm_obj_get_by_id(0)->payload = 1.0f;     // condition: true
  *(uint32_t*)vm_obj_get_by_id(1)->payload = 2u;    // selector: branch 2
  *(uint8_t*)vm_obj_get_by_id(2)->payload = 1;      // gate: open
  *(float*)vm_obj_get_by_id(3)->payload = 2.6f;     // rounds to branch 3, not 2
  *(float*)vm_obj_get_by_id(4)->payload = -1.0f;    // no such branch

  bool blk = true;
  blk = blk && br_block(0, VM_BLK_IF, 0, VM_BLOCK_NO_ID);
  blk = blk && br_block(1, VM_BLK_SWITCH, 1, VM_BLOCK_NO_ID);
  blk = blk && br_block(2, VM_BLK_SWITCH, 1, 2);  // the gated one
  blk = blk && br_block(3, VM_BLK_SWITCH, 3, VM_BLOCK_NO_ID);
  blk = blk && br_block(4, VM_BLK_SWITCH, 4, VM_BLOCK_NO_ID);
  blk = blk && br_block(5, VM_BLK_IF, 0, VM_BLOCK_NO_ID);              // q_cnt 1: no ELSE to drive
  blk = blk && br_block(6, VM_BLK_IF, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID); // nothing wired to the condition
  ck("routers built in execution order", blk);
  if (!blk) return;

  /* The property the whole design rests on: these blocks are pure shape. What
     the loader allocates is the header plus the three pin arrays and not one
     byte more, so there is no private state to go stale between passes. */
  vm_block_h b0 = vm_block_get_by_id(0);
  ck("a router carries no payload", b0 && b0->cfg.custom_len == 0 && vm_block_get_total_size(b0) == vm_block_calc_size(1, 2, 0, 0));

  /* ---- pass 1: everything decides ---- */
  vm_exec_reset_stats();
  vm_exec_pass();

  ck("a true condition takes IF", one_hot(0, 0) && br_eno(0));
  ck("a selector takes exactly its own branch", one_hot(1, 2) && br_eno(1));
  ck("an open gate lets the router decide", one_hot(2, 2) && br_eno(2));
  /* 2.6 lands on branch 3 rather than branch 2, because a float read into an
     int32 pin rounds -- vm_get_as_i32(), the same conversion every other pin
     in the program gets. A router that truncated would be the odd one out. */
  ck("a fractional selector rounds, as every pin read does", one_hot(3, 3) && br_eno(3));

  /* Out of range is a state rather than a fault: nothing is driven, the flow
     is withdrawn, and no error is raised -- at scan rate one would be
     thousands. So the block is *not* latched CFG_BAD, which is the difference
     between "this program is wrong" and "this value is". */
  ck("a negative selector takes no branch", one_hot(4, -1) && !br_eno(4));
  ck("...and is not a config fault", !cfg_bad(4));

  /* The asymmetry, which is what makes a branch output usable as an enable
     source: asserting flow is news, withdrawing it is not. A loud false would
     read as an arrival to an update-driven block below. */
  ck("the taken branch is driven loud", br_upd(1, 2) == 1);
  ck("...and the branches not taken are cleared quietly", br_upd(1, 0) == 0 && br_upd(1, 1) == 0 && br_upd(1, 3) == 0);

  /* Both shape faults are standing conditions -- the same thing is wrong every
     pass -- so they latch and report once per load, exactly as malformed
     expression bytecode does. */
  ck("an IF with no ELSE to drive is a config fault", cfg_bad(5) && !br_eno(5) && one_hot(5, -1));
  ck("an unwired condition is a config fault", cfg_bad(6) && !br_eno(6) && one_hot(6, -1));
  ck("...and a well-formed router is not latched", !cfg_bad(0) && !cfg_bad(1) && !cfg_bad(2));

  /* ---- pass 2: the condition flips, the selection moves, the gate closes ---- */
  *(float*)vm_obj_get_by_id(0)->payload = 0.0f;
  *(uint32_t*)vm_obj_get_by_id(1)->payload = 0u;
  *(uint8_t*)vm_obj_get_by_id(2)->payload = 0;
  /* These branch objects are not upd_resetable, so the end-of-pass sweep never
     touches them and pass 1's loud write is still standing. Clearing by hand is
     what makes the next two assertions about *this* pass rather than the last. */
  for (uint8_t k = 0; k < 4; k++) {
    vm_obj_get_by_id(BR_OUT(1, k))->head.f.upd = 0;
    vm_obj_get_by_id(BR_OUT(2, k))->head.f.upd = 0;
  }
  vm_exec_pass();

  ck("a false condition takes ELSE", one_hot(0, 1) && br_eno(0));
  ck("a moved selection releases the branch it left", one_hot(1, 0) && br_eno(1));
  ck("...the branch it left going quiet, not loud", br_upd(1, 0) == 1 && br_upd(1, 2) == 0);

  /* THE ONE THAT MATTERS. A router's outputs feed enable lists, so a stale
     branch is not a stale value -- it is a whole subtree still running behind
     a gate that has closed. This is why these blocks break the template's
     "outputs stand" rule, and it is the only assertion here that the rule as
     written would fail. */
  ck("a closed gate withdraws every branch", one_hot(2, -1) && !br_eno(2));
  ck("...quietly, so nothing below reads it as an arrival", br_upd(2, 2) == 0);

  /* ---- pass 3: the selector leaves the case set entirely ---- */
  *(uint32_t*)vm_obj_get_by_id(1)->payload = 9u;
  vm_exec_pass();

  ck("a selector past the last case takes no branch", one_hot(1, -1) && !br_eno(1));
  ck("...and still is not a config fault", !cfg_bad(1));

  /* ---- pass 4: and comes back ---- */
  *(uint32_t*)vm_obj_get_by_id(1)->payload = 3u;
  *(uint8_t*)vm_obj_get_by_id(2)->payload = 1;
  vm_exec_pass();

  ck("a selector returning to range routes again", one_hot(1, 3) && br_eno(1));
  ck("a gate reopening routes again", one_hot(2, 3) && br_eno(2));
}

/* ==========================================================================
   Stage V -- FOR, the span owner

   The one block that runs other blocks, so this is as much about the walk as
   about the block. Everything here is counted rather than sampled, because the
   only way to tell "the owner ran it three times" from "the owner ran it three
   times and the walk ran it again" is to count -- and the counter is itself a
   block out of the palette (ex_counter(), stage R): an EXPR reading the object
   it writes and adding one to it, once per dispatch.

   Registry id == block_idx == position in the execution order, and here that is
   load-bearing rather than a convention: a span must start at the block
   immediately after its owner or the walk rejects the claim.

     0  FOR  i=0; i<3; i+=1             1  CNT   3 calls
     2  FOR  same, gated off            3  CNT   0 calls
     4  FOR  i=0; i<END(pin); i+=1      5  CNT   the pin drives it, budget 5
     6  FOR  i=0; i<2; i+=1             7  CNT   2 calls -- once per outer turn
                                        8  FOR   i=0; i<3; i+=1
                                        9  CNT   2*3 = 6 calls
    10  FOR  i=0; i<2; i+=1            11  CNT   2 calls
                                       12  EXPR  bad opcode -- fails every call
    13  FOR  custom_len 4              14  CNT   0 calls -- claimable, unreadable
    15  FOR  i=0; i<4; i+=1            16  EXPR  acc = acc + i
    17  FOR  i=10; i>0; i-=2           18  CNT   5 calls -- descending
    19  FOR  i=1; i<100; i*=2          20  CNT   7 calls -- geometric
    21  FOR  i=0; i<3; i+=0            22  CNT   4 calls -- never ends; the budget does

   The outer span at 6 holds a plain block *and* a nested loop, which is both the
   realistic shape and the only way to count outer turns: a FOR carries
   vm_for_code_t and publishes only its iterator, so it cannot count itself.

   The span at 10 holds two blocks for the same reason: a body block that fails
   never publishes, so the failure and the count have to be separate blocks.
   ========================================================================== */

#define FOR_ENO(n) ((uint16_t)(20 + (n)))  // one per block, 20..42
#define FOR_CNT(n) ((uint16_t)(43 + (n)))  // the body counters, sparse by design
#define FOR_IDX0 70u   // block 0's iterator
#define FOR_IDX15 71u  // block 15's, which the fold reads
#define FOR_ACC 72u    // the accumulator block 16 folds into
#define FOR_END 73u    // the live end value block 4 reads
#define FOR_GATE 74u   // block 2's enable, held false
#define FOR_IDXD 75u   // block 17's, descending
#define FOR_IDXM 76u   // block 19's, geometric
#define FOR_SINK 77u   // block 12's output, which it never reaches

/* Every block in the table above that is a counter. Its accessor id is its
   position here plus 4 -- accessors 0..3 belong to the loops themselves. */
static const uint8_t s_for_bodies[] = {1, 3, 5, 7, 9, 11, 14, 18, 20, 22};
#define FOR_BODY_CNT ((uint8_t)(sizeof(s_for_bodies) / sizeof(s_for_bodies[0])))

static bool for_eno(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(FOR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

static uint32_t for_idx(uint16_t obj_id) {
  vm_obj_h o = vm_obj_get_by_id(obj_id);
  return o ? *(uint32_t*)o->payload : 0xFFFFFFFFu;
}

// how many times the supervisor called body block `n`
static uint32_t for_cnt(uint16_t n) {
  return ex_count(FOR_CNT(n));
}

static void for_counters_reset(void) {
  for (uint8_t i = 0; i < FOR_BODY_CNT; i++) ex_count_reset(FOR_CNT(s_for_bodies[i]));
}

// the block's own view of its bad-loop episode -- VM_FOR_RT_BAD
static uint8_t for_rt(uint16_t n) {
  vm_block_h b = vm_block_get_by_id(n);
  if (!b || b->cfg.custom_len < sizeof(vm_for_code_t)) return 0xFFu;
  return ((const vm_for_code_t*)vm_block_get_custom_data(b))->rt;
}

// a loop with no pins and no iterator output, which most of the table is
static bool for_plain(uint16_t id, uint16_t span_start, uint16_t span_end, for_loop_t lp) {
  return ex_for(id, span_start, span_end, lp, NULL, 0, NULL, 0, NULL, 0, FOR_ENO(id), sizeof(vm_for_code_t));
}

// ...and one that publishes its iterator, so the body has something to read
static bool for_idxed(uint16_t id, uint16_t span_start, uint16_t span_end, for_loop_t lp, uint16_t idx_obj) {
  return ex_for(id, span_start, span_end, lp, NULL, 0, (const uint16_t[]){idx_obj}, 1, NULL, 0, FOR_ENO(id),
                sizeof(vm_for_code_t));
}

void test_for(void) {
  ESP_LOGI(TAG, "-- V: FOR and the span walk --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 80, [VM_REG_ACC] = 16, [VM_REG_BLK] = 24};
  (void)vm_store_open(8192, counts);

  bool built = true;
  for (uint16_t n = 0; n <= 22; n++) built = built && mk(FOR_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  for (uint8_t i = 0; i < FOR_BODY_CNT; i++) built = built && mk(FOR_CNT(s_for_bodies[i]), VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(FOR_IDX0, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(FOR_IDX15, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(FOR_ACC, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(FOR_END, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(FOR_GATE, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(FOR_IDXD, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(FOR_IDXM, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk(FOR_SINK, VM_OBJ_F, 1, NULL, true) != NULL;
  ck("objects built", built);

  bool accs = true;
  accs = accs && ex_acc(0, FOR_END) != NULL;
  accs = accs && ex_acc(1, FOR_GATE) != NULL;
  accs = accs && ex_acc(2, FOR_ACC) != NULL;
  accs = accs && ex_acc(3, FOR_IDX15) != NULL;
  for (uint8_t i = 0; i < FOR_BODY_CNT; i++) accs = accs && ex_acc((uint16_t)(4 + i), FOR_CNT(s_for_bodies[i])) != NULL;
  ck("accessors built", accs);

  *(uint32_t*)vm_obj_get_by_id(FOR_END)->payload = 3u;
  *(uint8_t*)vm_obj_get_by_id(FOR_GATE)->payload = 0;  // block 2 stays shut

  static const uint8_t c_fold[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_ADD};  // acc + i
  static const uint8_t c_badop[] = {VM_EXPR_IN, 0, 0xFE};                       // fails on every call

  // for (i = 0; i < 3; i += 1), and the shapes that differ from it
  const for_loop_t up3 = {.start = 0, .end = 3, .step = 1, .budget = 8, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};
  const for_loop_t up2 = {.start = 0, .end = 2, .step = 1, .budget = 8, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};
  const for_loop_t up4 = {.start = 0, .end = 4, .step = 1, .budget = 8, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};
  const for_loop_t pin = {.start = 0, .end = 0, .step = 1, .budget = 5, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};
  const for_loop_t down = {.start = 10, .end = 0, .step = 2, .budget = 16, .op = VM_FOR_OP_SUB, .cmp = VM_FOR_CMP_GT};
  const for_loop_t geo = {.start = 1, .end = 100, .step = 2, .budget = 16, .op = VM_FOR_OP_MUL, .cmp = VM_FOR_CMP_LT};
  const for_loop_t stuck = {.start = 0, .end = 3, .step = 0, .budget = 4, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};

  /* The bodies first -- creation order is free, since a block's id is both its
     registry slot and its place in the walk. */
  bool blk = true;
  for (uint8_t i = 0; i < FOR_BODY_CNT; i++) {
    blk = blk && ex_counter(s_for_bodies[i], (uint16_t)(4 + i), FOR_CNT(s_for_bodies[i]));
  }

  blk = blk && for_idxed(0, 1, 2, up3, FOR_IDX0);
  blk = blk && ex_for(2, 3, 4, up3, NULL, 0, NULL, 0, (const uint16_t[]){1}, 1, FOR_ENO(2), sizeof(vm_for_code_t));
  // start unwired (falls back to its constant), end wired -- both paths at once
  blk = blk && ex_for(4, 5, 6, pin, (const uint16_t[]){VM_BLOCK_NO_ID, 0}, 2, NULL, 0, NULL, 0, FOR_ENO(4),
                      sizeof(vm_for_code_t));
  blk = blk && for_plain(6, 7, 10, up2);
  blk = blk && for_plain(8, 9, 10, up3);
  blk = blk && for_plain(10, 11, 13, up2);
  /* Reads block 11's counter only to have a fresh pin: an EXPR that never
     triggers never evaluates, so a body that is supposed to fail every call
     has to be given something to arrive. */
  blk = blk && ex_expr(12, (const uint16_t[]){9}, 1, FOR_SINK, FOR_ENO(12), NULL, 0, c_badop, sizeof(c_badop));
  // a loop that produces 0 turns: claims its span and leaves body untouched
  const for_loop_t zero_turns = {.start = 0, .end = 0, .step = 1, .budget = 4, .op = VM_FOR_OP_ADD, .cmp = VM_FOR_CMP_LT};
  blk = blk && ex_for(13, 14, 15, zero_turns, NULL, 0, NULL, 0, NULL, 0, FOR_ENO(13), sizeof(vm_for_code_t));
  ck("for loop invalid op rejected at build",
     !ex_for(98, 0, 1, (for_loop_t){.op = VM_FOR_OP_CNT}, NULL, 0, NULL, 0, NULL, 0, VM_BLOCK_NO_ID, sizeof(vm_for_code_t)));
  ck("for loop short payload rejected at build",
     !ex_for(99, 0, 1, up2, NULL, 0, NULL, 0, NULL, 0, VM_BLOCK_NO_ID, sizeof(vm_span_t)));
  blk = blk && for_idxed(15, 16, 17, up4, FOR_IDX15);
  blk = blk && ex_expr(16, (const uint16_t[]){2, 3}, 2, FOR_ACC, FOR_ENO(16), NULL, 0, c_fold, sizeof(c_fold));
  blk = blk && for_idxed(17, 18, 19, down, FOR_IDXD);
  blk = blk && for_idxed(19, 20, 21, geo, FOR_IDXM);
  blk = blk && for_plain(21, 22, 23, stuck);
  ck("loops and bodies built in execution order", blk);
  if (!blk) return;

  /* ---- pass 1 ---- */
  vm_exec_reset_stats();
  vm_exec_pass();

  /* Three, not four. Four would mean the outer walk ran the body itself as well
     as the owner running it -- which is the whole reason a claim exists. */
  ck("for (i=0; i<3; i+=1) runs its body three times", for_cnt(1) == 3);
  ck("...publishing the iterator, so it ends holding the last one", for_idx(FOR_IDX0) == 2u);
  ck("...with the flow asserted", for_eno(0));

  /* Zero, not one. A disabled FOR that returned before claiming would leave the
     walk about to run its body inline, and running a span zero times is not the
     same as letting somebody else run it once. */
  ck("a disabled loop claims first, so its body runs no times", for_cnt(3) == 0 && !for_eno(2));

  ck("an unwired start reads its constant while a wired end drives the loop", for_cnt(5) == 3 && for_eno(4));
  ck("...and a loop that ended on its own is not latched", (for_rt(4) & VM_FOR_RT_BAD) == 0);

  ck("a plain block in a span runs once per turn", for_cnt(7) == 2);
  ck("...and a loop nested beside it runs the product", for_cnt(9) == 6);

  /* The engine's own bug, and the only block that could ever have found it:
     g_vm_block_fault is one global, and the body's blocks run through the same
     run_block() the owner is currently inside. Without saving it, the last
     block in the span decides the owner's on_error -- so this FOR would lose
     its ENO because something it ran failed, after that block's own on_error
     had already handled it. */
  ck("a failing body block does not withdraw its owner's flow", for_cnt(11) == 2 && for_eno(10));
  ck("...though it does withdraw its own", !for_eno(12) && cfg_bad(12));

  /* A loop with 0 turns claims its span, so its body stays put and does not run. */
  ck("a zero-turn loop claims first, so its body stays put", for_cnt(14) == 0 && !for_eno(13));
  ck("...and is not latched as a fault", !cfg_bad(13));
  ck("...while the well-formed loops are not", !cfg_bad(0) && !cfg_bad(6) && !cfg_bad(17));

  /* The point of the whole mechanism: a body that varies per turn, and a fold
     across turns. `upd` is swept at the end of the *pass*, not per turn, so the
     accumulator reads its own last value and re-triggers -- 0+1+2+3. */
  ck("a body accumulates across turns", near_f(*(float*)vm_obj_get_by_id(FOR_ACC)->payload, 6.0f));

  /* The three shapes a plain repeat count cannot say. */
  ck("for (i=10; i>0; i-=2) counts down", for_cnt(18) == 5 && for_idx(FOR_IDXD) == 2u);
  ck("for (i=1; i<100; i*=2) steps geometrically", for_cnt(20) == 7 && for_idx(FOR_IDXM) == 64u);

  /* A step of zero never makes the condition false. On a scan-cycle task that is
     a hang, and the block watchdog cannot see it -- it names whichever body
     block is running, and those keep changing. The budget is what ends it. */
  ck("a loop that cannot end runs its budget and no more", for_cnt(22) == 4);
  ck("...and says so, once", (for_rt(21) & VM_FOR_RT_BAD) != 0);

  /* ---- pass 2: the live end asks for more turns than the budget ---- */
  for_counters_reset();
  *(float*)vm_obj_get_by_id(FOR_ACC)->payload = 0.0f;
  *(uint32_t*)vm_obj_get_by_id(FOR_END)->payload = 9u;
  vm_exec_pass();

  ck("a pin asking past the budget gets the budget", for_cnt(5) == 5);
  ck("...and is latched, so a stuck loop reports once", (for_rt(4) & VM_FOR_RT_BAD) != 0);
  ck("the fold repeats identically on the next pass", near_f(*(float*)vm_obj_get_by_id(FOR_ACC)->payload, 6.0f));

  /* ---- pass 3: back inside the budget ---- */
  for_counters_reset();
  *(uint32_t*)vm_obj_get_by_id(FOR_END)->payload = 2u;
  vm_exec_pass();

  ck("an end the loop can reach is honoured", for_cnt(5) == 2);
  ck("...and ending properly re-arms the report", (for_rt(4) & VM_FOR_RT_BAD) == 0);

  /* ---- pass 4: the condition is false before the first turn ---- */
  for_counters_reset();
  *(uint32_t*)vm_obj_get_by_id(FOR_END)->payload = 0u;
  vm_exec_pass();

  ck("a condition false at the start runs the body no times", for_cnt(5) == 0 && !for_eno(4));
  ck("...while the fixed loops are unaffected", for_cnt(1) == 3 && for_cnt(9) == 6);
}

/* ==========================================================================
   Stage W -- the Set block

   The first block whose *target* is an input pin, and everything here follows
   from that. A Set has no data output: what it produces is named by the wire
   drawn into IN1, so ENO is the only thing it publishes and it says one thing
   -- the copy happened this pass.

   The assertion that matters most is the fresh-target one at the end. Reading
   freshness off IN0 alone rather than through vm_block_triggered() is what
   stops a Set from re-firing on the object it just wrote, and since every copy
   refreshes the target, getting that wrong is not a one-pass glitch but a copy
   that never stops.
   ========================================================================== */

#define SET_SRC 0    // F, arrives fresh -- the common source
#define SET_DST 1    // F, the common target
#define SET_DST2 2   // F, target of the gated Set
#define SET_GATE 3   // B, enable source, held shut
#define SET_BAD 4    // U32, the wrong type to take a float
#define SET_ARRS 5   // F[4] source
#define SET_ARRD 6   // F[4] target
#define SET_PTRS 7   // PTR source -- an alias, not a copy
#define SET_PTRD 8   // PTR target
#define SET_SRC1 9   // F, goes stale for pass 2
#define SET_DST1 10  // F, and whose target goes stale with it
#define SET_SRC8 11  // F, goes stale for pass 2
#define SET_DST8 12  // F, but whose target stays fresh

#define SET_T_SRC 13  // PTR[2] source table -- the 2D case
#define SET_T_DST 14  // PTR[2] target table, with rows of its own
#define SET_R_S0 15   // F[3] source rows
#define SET_R_S1 16
#define SET_R_D0 17   // F[3] target rows
#define SET_R_D1 18
#define SET_T_BAD 19  // PTR[2] whose second slot was never wired
#define SET_PL_S 20   // F leaf under SET_PTRS -- the one-level deep copy
#define SET_PL_D 21   // F leaf under SET_PTRD

/* Shapes the walk has to refuse rather than follow. Each is reachable from a
   program the loader would accept, which is why they are worth building. */
#define SET_CYC_A 22      // src cycle: A -> B -> A
#define SET_CYC_B 23
#define SET_CYC_C 24      // dst cycle: C -> D -> C, same shape all the way down
#define SET_CYC_D 25
#define SET_T_SEMPTY 26   // PTR[2] whose slot 1 was never wired
#define SET_T_ROMUT 27    // PTR[2] whose row 0 cannot be written
#define SET_R_RO 28       // that row
#define SET_T_NARROW 29   // PTR[2] whose row 0 is F[2] where the source has F[3]
#define SET_R_NARROW 30
#define SET_T_SHARE 31    // two tables that already share row 0
#define SET_T_SHARE2 32
#define SET_R_SHARE 33    // row 1 of SHARE2, the only one with copying to do
#define SET_L3_S 34       // PTR[1] -> SET_T_SRC : three levels, not two
#define SET_L3_D 35       // PTR[1] -> SET_T_DST

#define SET_GATE_OPEN 36  // an enable that is actually on
#define SET_DST3 37       // its target
#define SET_RO_DST 38     // a target the block may not write
#define SET_ELEM_DST 39   // F[4]; block 13 targets element 2 of it

#define SET_ENO(n) ((uint16_t)(60 + (n)))

/* A source with a real arrival: mutable, upd_resetable and primed fresh, so
   pass 1 sees news and the end-of-pass sweep takes it away again. That second
   part is what makes a second pass a genuine stale case -- an mk() object is
   not resetable, so its upd would stand for the rest of the run. */
static vm_obj_h mk_ev(uint16_t id, vm_obj_t_e type, uint16_t items) {
  vm_obj_head_t h = hd(type, items);
  h.f.mutable = 1;
  h.f.upd_resetable = 1;
  vm_obj_h o = NULL;
  if (vm_obj_create(&o, id, &h, NULL) != NULL) return NULL;
  o->head.f.upd = 1;
  return o;
}

// accessor ids run 1:1 with object ids in this stage, so a pin names its object
/* `eno` is a parameter rather than SET_ENO(id): stage Y builds Set blocks too,
   with an object numbering of its own, and a helper that picks the ENO for its
   caller silently names an object that stage does not have. */
static bool set_blk(uint16_t id, const uint16_t* ins, uint8_t in_cnt, uint16_t en_acc, uint16_t eno) {
  const bool gated = (en_acc != VM_BLOCK_NO_ID);
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_SET,
                                              .in_cnt = in_cnt,
                                              .q_cnt = 0,
                                              .en_cnt = gated ? 1 : 0,
                                              .en_mode = VM_BLK_EN_ALL,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = VM_SET_CUSTOM_LEN,
                                              .in_acc_ids = ins,
                                              .en_acc_ids = gated ? &en_acc : NULL,
                                              .eno_obj_id = eno});
  return e == NULL && b != NULL;
}

static float set_f(uint16_t obj) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o ? *(float*)o->payload : -1.0f;
}

static float set_at(uint16_t obj, uint8_t i) {
  vm_obj_h o = vm_obj_get_by_id(obj);
  return o ? ((const float*)o->payload)[i] : -1.0f;
}

static bool set_eno(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(SET_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

void test_set(void) {
  ESP_LOGI(TAG, "-- W: the Set block --");
  vm_loader_reset();
  /* Accessor ids track object ids in this stage, so the accessor registry has
     to reach as far as the highest object an accessor is rooted at, not merely
     as far as the number of accessors. */
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 80, [VM_REG_ACC] = 48, [VM_REG_BLK] = 20};
  (void)vm_store_open(16384, counts);

  bool built = true;
  built = built && mk_ev(SET_SRC, VM_OBJ_F, 1) != NULL;
  built = built && mk(SET_DST, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(SET_DST2, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(SET_GATE, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(SET_BAD, VM_OBJ_U32, 1, NULL, true) != NULL;
  built = built && mk_ev(SET_ARRS, VM_OBJ_F, 4) != NULL;
  built = built && mk(SET_ARRD, VM_OBJ_F, 4, NULL, true) != NULL;
  built = built && mk_ev(SET_PTRS, VM_OBJ_PTR, 1) != NULL;
  built = built && mk(SET_PTRD, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk_ev(SET_SRC1, VM_OBJ_F, 1) != NULL;
  built = built && mk(SET_DST1, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk_ev(SET_SRC8, VM_OBJ_F, 1) != NULL;
  built = built && mk(SET_DST8, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk_ev(SET_T_SRC, VM_OBJ_PTR, 2) != NULL;
  built = built && mk(SET_T_DST, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_R_S0, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(SET_R_S1, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(SET_R_D0, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(SET_R_D1, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(SET_T_BAD, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_PL_S, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(SET_PL_D, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk_ev(SET_CYC_A, VM_OBJ_PTR, 1) != NULL;
  built = built && mk(SET_CYC_B, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(SET_CYC_C, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(SET_CYC_D, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(SET_T_SEMPTY, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_T_ROMUT, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_R_RO, VM_OBJ_F, 3, NULL, false) != NULL;
  built = built && mk(SET_T_NARROW, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_R_NARROW, VM_OBJ_F, 2, NULL, true) != NULL;
  built = built && mk(SET_T_SHARE, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_T_SHARE2, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(SET_R_SHARE, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(SET_L3_S, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(SET_L3_D, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(SET_GATE_OPEN, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(SET_DST3, VM_OBJ_F, 1, NULL, true) != NULL;
  built = built && mk(SET_RO_DST, VM_OBJ_F, 1, NULL, false) != NULL;
  built = built && mk(SET_ELEM_DST, VM_OBJ_F, 4, NULL, true) != NULL;
  for (uint16_t n = 0; n <= 13; n++) built = built && mk(SET_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  ck("objects built", built);

  /* Two 2 x 3 tables, each with its own rows -- which is the shape a deep copy
     needs and the whole reason it can allocate nothing. SET_T_BAD gets one row
     and is left a slot short on purpose. */
  bool wired = built;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SRC), 0, vm_obj_get_by_id(SET_R_S0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SRC), 1, vm_obj_get_by_id(SET_R_S1)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_DST), 0, vm_obj_get_by_id(SET_R_D0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_DST), 1, vm_obj_get_by_id(SET_R_D1)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_BAD), 0, vm_obj_get_by_id(SET_R_D0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_PTRS), 0, vm_obj_get_by_id(SET_PL_S)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_PTRD), 0, vm_obj_get_by_id(SET_PL_D)) == NULL;
  // two cycles of equal shape -- the walk cannot tell them apart from a deep tree
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_CYC_A), 0, vm_obj_get_by_id(SET_CYC_B)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_CYC_B), 0, vm_obj_get_by_id(SET_CYC_A)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_CYC_C), 0, vm_obj_get_by_id(SET_CYC_D)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_CYC_D), 0, vm_obj_get_by_id(SET_CYC_C)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SEMPTY), 0, vm_obj_get_by_id(SET_R_S0)) == NULL;  // slot 1 left empty
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_ROMUT), 0, vm_obj_get_by_id(SET_R_RO)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_ROMUT), 1, vm_obj_get_by_id(SET_R_D1)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_NARROW), 0, vm_obj_get_by_id(SET_R_NARROW)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_NARROW), 1, vm_obj_get_by_id(SET_R_D1)) == NULL;
  // SHARE and SHARE2 already name the same row 0: nothing to move there
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SHARE), 0, vm_obj_get_by_id(SET_R_S0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SHARE), 1, vm_obj_get_by_id(SET_R_S1)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SHARE2), 0, vm_obj_get_by_id(SET_R_S0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_T_SHARE2), 1, vm_obj_get_by_id(SET_R_SHARE)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_L3_S), 0, vm_obj_get_by_id(SET_T_SRC)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(SET_L3_D), 0, vm_obj_get_by_id(SET_T_DST)) == NULL;
  ck("tables wired", wired);
  for (uint8_t i = 0; i < 3; i++) {
    ((float*)vm_obj_get_by_id(SET_R_S0)->payload)[i] = (float)(i + 1);  // 1 2 3
    ((float*)vm_obj_get_by_id(SET_R_S1)->payload)[i] = (float)(i + 4);  // 4 5 6
  }
  vm_obj_get_by_id(SET_T_SRC)->head.f.upd = 1;  // link_direct already set it; say so out loud

  *(uint8_t*)vm_obj_get_by_id(SET_GATE_OPEN)->payload = 1;  // block 11 is armed

  bool accs = true;
  for (uint16_t i = SET_SRC; i <= SET_T_BAD; i++) accs = accs && ex_acc(i, i) != NULL;
  accs = accs && ex_acc(SET_GATE_OPEN, SET_GATE_OPEN) != NULL;
  accs = accs && ex_acc(SET_DST3, SET_DST3) != NULL;
  accs = accs && ex_acc(SET_RO_DST, SET_RO_DST) != NULL;
  /* Element 2 of an array as a *destination* -- a Set target is an ordinary
     accessor, so it can name one slot as readily as a whole object. */
  vm_accessor_t* a_elem = NULL;
  accs = accs && vm_accessor_create(&a_elem, SET_ELEM_DST, SET_ELEM_DST, 1) == NULL;
  accs = accs && vm_accessor_set_literal(a_elem, 0, 2) == NULL;
  if (accs) (void)vm_accessor_cache_build(a_elem);
  ck("accessors built", accs);

  *(float*)vm_obj_get_by_id(SET_SRC)->payload = 12.5f;
  *(float*)vm_obj_get_by_id(SET_SRC1)->payload = 1.0f;
  *(float*)vm_obj_get_by_id(SET_SRC8)->payload = 2.0f;
  for (uint8_t i = 0; i < 4; i++) ((float*)vm_obj_get_by_id(SET_ARRS)->payload)[i] = (float)(i + 1);
  *(uint8_t*)vm_obj_get_by_id(SET_GATE)->payload = 0;  // block 2 stays shut
  *(float*)vm_obj_get_by_id(SET_PL_S)->payload = 8.5f;
  vm_obj_get_by_id(SET_PTRS)->head.f.upd = 1;

  bool blk = true;
  blk = blk && set_blk(0, (const uint16_t[]){SET_SRC, SET_DST}, 2, VM_BLOCK_NO_ID, SET_ENO(0));
  blk = blk && set_blk(1, (const uint16_t[]){SET_SRC1, SET_DST1}, 2, VM_BLOCK_NO_ID, SET_ENO(1));
  blk = blk && set_blk(2, (const uint16_t[]){SET_SRC, SET_DST2}, 2, SET_GATE, SET_ENO(2));
  blk = blk && set_blk(3, (const uint16_t[]){SET_SRC, SET_BAD}, 2, VM_BLOCK_NO_ID, SET_ENO(3));
  blk = blk && set_blk(4, (const uint16_t[]){SET_ARRS, SET_ARRD}, 2, VM_BLOCK_NO_ID, SET_ENO(4));
  blk = blk && set_blk(5, (const uint16_t[]){SET_PTRS, SET_PTRD}, 2, VM_BLOCK_NO_ID, SET_ENO(5));
  blk = blk && set_blk(6, (const uint16_t[]){SET_SRC}, 1, VM_BLOCK_NO_ID, SET_ENO(6));
  blk = blk && set_blk(7, (const uint16_t[]){SET_SRC, VM_BLOCK_NO_ID}, 2, VM_BLOCK_NO_ID, SET_ENO(7));
  blk = blk && set_blk(8, (const uint16_t[]){SET_SRC8, SET_DST8}, 2, VM_BLOCK_NO_ID, SET_ENO(8));
  blk = blk && set_blk(9, (const uint16_t[]){SET_T_SRC, SET_T_DST}, 2, VM_BLOCK_NO_ID, SET_ENO(9));
  blk = blk && set_blk(10, (const uint16_t[]){SET_T_SRC, SET_T_BAD}, 2, VM_BLOCK_NO_ID, SET_ENO(10));
  blk = blk && set_blk(11, (const uint16_t[]){SET_SRC, SET_DST3}, 2, SET_GATE_OPEN, SET_ENO(11));
  blk = blk && set_blk(12, (const uint16_t[]){SET_SRC, SET_RO_DST}, 2, VM_BLOCK_NO_ID, SET_ENO(12));
  blk = blk && set_blk(13, (const uint16_t[]){SET_SRC, SET_ELEM_DST}, 2, VM_BLOCK_NO_ID, SET_ENO(13));
  ck("Set blocks built in execution order", blk);
  if (!blk) return;

  /* ---- pass 1 ---- */
  vm_exec_pass();

  ck("a fresh source copies into the target", near_f(set_f(SET_DST), 12.5f) && set_eno(0));
  ck("...and the copy is loud, so a reader downstream sees news", vm_obj_get_by_id(SET_DST)->head.f.upd != 0);
  ck("...and the block latched that it was triggered", (vm_block_get_by_id(0)->cfg.rt & VM_BLK_RT_TRIGGERED) != 0);

  /* A Set moves the whole payload, so an array arrives as an array. This is
     the case a scalar store could not do at all. */
  ck("a whole array moves, not just element 0",
     near_f(set_at(SET_ARRD, 0), 1.0f) && near_f(set_at(SET_ARRD, 1), 2.0f) && near_f(set_at(SET_ARRD, 2), 3.0f) &&
         near_f(set_at(SET_ARRD, 3), 4.0f) && set_eno(4));

  // update-driven and enable-driven compose: fresh is necessary, not sufficient
  ck("a gated Set does not copy however fresh the source", near_f(set_f(SET_DST2), 0.0f) && !set_eno(2));

  /* Every refusal has the same shape: nothing published, flow withdrawn. */
  ck("a type mismatch is refused", *(uint32_t*)vm_obj_get_by_id(SET_BAD)->payload == 0u && !set_eno(3));
  ck("one pin is not a Set", !set_eno(6) && cfg_bad(6));
  ck("an unwired target is not a Set", !set_eno(7) && cfg_bad(7));
  ck("...and neither of those is latched on a well-formed Set", !cfg_bad(0) && !cfg_bad(4));
  ck("the sweep clears a resetable source's upd", vm_obj_get_by_id(SET_SRC1)->head.f.upd == 0);

  /* One pointer cell is the smallest deep copy there is: the value under the
     source slot lands under the target slot, and the two slots still name
     different objects afterwards. A byte copy would have made them one. */
  ck("a pointer cell is walked, so the value under it moves", near_f(set_f(SET_PL_D), 8.5f) && set_eno(5));
  ck("...and the two cells still point at objects of their own",
     ((const vm_obj_h*)vm_obj_get_by_id(SET_PTRS)->payload)[0] != ((const vm_obj_h*)vm_obj_get_by_id(SET_PTRD)->payload)[0]);

  /* ---- the 2D case ---- */
  ck("a 2D table copies row by row",
     near_f(set_at(SET_R_D0, 0), 1.0f) && near_f(set_at(SET_R_D0, 2), 3.0f) && near_f(set_at(SET_R_D1, 0), 4.0f) &&
         near_f(set_at(SET_R_D1, 2), 6.0f) && set_eno(9));
  ck("...marking both the rows written and the table above them fresh",
     vm_obj_get_by_id(SET_R_D0)->head.f.upd != 0 && vm_obj_get_by_id(SET_T_DST)->head.f.upd != 0);

  /* The whole reason the walk exists. A shallow copy of the pointer bytes
     would have left both tables naming the same rows, and this edit would
     show up in the source -- somewhere else entirely in the program, long
     after the Set ran. */
  ck("the two tables kept their own rows",
     ((const vm_obj_h*)vm_obj_get_by_id(SET_T_SRC)->payload)[0] != ((const vm_obj_h*)vm_obj_get_by_id(SET_T_DST)->payload)[0]);
  ((float*)vm_obj_get_by_id(SET_R_D0)->payload)[0] = -1.0f;
  ck("...so editing the copy does not reach the original", near_f(set_at(SET_R_S0, 0), 1.0f));

  ck("a target slot with no object is a shape error, not a crash", !set_eno(10) && near_f(set_at(SET_R_S1, 0), 4.0f));

  /* ---- the rest of the block's activation surface ---- */
  ck("an enable that is on lets the copy through", near_f(set_f(SET_DST3), 12.5f) && set_eno(11));
  ck("a target the block may not write is refused", !set_eno(12));
  /* A Set target is an ordinary accessor, so it can name one element as
     readily as a whole object -- and only that element moves. */
  ck("a Set can target a single element", near_f(set_at(SET_ELEM_DST, 2), 12.5f) && set_eno(13));
  ck("...leaving its neighbours alone", near_f(set_at(SET_ELEM_DST, 1), 0.0f) && near_f(set_at(SET_ELEM_DST, 3), 0.0f));

  /* ---- the walk's limits, called directly. These are shapes a loaded program
          can genuinely hold, so the walk has to refuse them rather than follow
          them; static accessors because the subject is vm_obj_copy_content(),
          not a block. ---- */
  static const vm_accessor_t w_cyc_s = {.id = SET_CYC_A, .count = 0, .indices = NULL};
  static const vm_accessor_t w_cyc_d = {.id = SET_CYC_C, .count = 0, .indices = NULL};
  static const vm_accessor_t w_tsrc = {.id = SET_T_SRC, .count = 0, .indices = NULL};
  static const vm_accessor_t w_sempty = {.id = SET_T_SEMPTY, .count = 0, .indices = NULL};
  static const vm_accessor_t w_tdst = {.id = SET_T_DST, .count = 0, .indices = NULL};
  static const vm_accessor_t w_romut = {.id = SET_T_ROMUT, .count = 0, .indices = NULL};
  static const vm_accessor_t w_narrow = {.id = SET_T_NARROW, .count = 0, .indices = NULL};
  static const vm_accessor_t w_share = {.id = SET_T_SHARE, .count = 0, .indices = NULL};
  static const vm_accessor_t w_share2 = {.id = SET_T_SHARE2, .count = 0, .indices = NULL};
  static const vm_accessor_t w_l3s = {.id = SET_L3_S, .count = 0, .indices = NULL};
  static const vm_accessor_t w_l3d = {.id = SET_L3_D, .count = 0, .indices = NULL};

  /* Two cycles of matching shape. Nothing about them is locally wrong -- every
     level agrees with its opposite -- so only the depth cap ends the walk, and
     ending it is the whole reason the cap exists. */
  ck("a cycle is stopped by the depth cap rather than the stack", vm_obj_copy_content(&w_cyc_s, &w_cyc_d) != NULL);

  ck("a source slot with no object is a shape error too", vm_obj_copy_content(&w_sempty, &w_tdst) != NULL);
  ck("a row that cannot be written stops the walk", vm_obj_copy_content(&w_tsrc, &w_romut) != NULL);
  /* Depth 0 agrees -- both are PTR[2] -- and the mismatch is a level down,
     which is the case a non-recursive check would have missed. */
  ck("a mismatch below the root is still caught", vm_obj_copy_content(&w_tsrc, &w_narrow) != NULL);
  ck("...and the rows it could not take are untouched", near_f(set_at(SET_R_NARROW, 0), 0.0f));

  /* Row 0 is already the same object on both sides: there is nothing to move
     and copying it onto itself would be a memcpy over itself. Row 1 differs
     and does move, so the skip cannot be the walk simply giving up. */
  ck("a child both trees already share is skipped, not copied over itself",
     vm_obj_copy_content(&w_share, &w_share2) == NULL && near_f(set_at(SET_R_SHARE, 2), 6.0f));

  ck("three levels walk as readily as two", vm_obj_copy_content(&w_l3s, &w_l3d) == NULL);

  /* ---- pass 2: nothing arrives. Both sources hold a value they would copy
          if they ran, so a copy that happens anyway is visible. ---- */
  *(float*)vm_obj_get_by_id(SET_SRC1)->payload = 99.0f;
  *(float*)vm_obj_get_by_id(SET_SRC8)->payload = 99.0f;
  vm_obj_get_by_id(SET_DST1)->head.f.upd = 0;  // block 1's target goes stale with its source
  /* Pass 1's true ENO left `upd` standing: set_ENO(false) clears the payload
     quietly and by design does not touch the flag, and an mk() object is not
     upd_resetable so the sweep leaves it too. Clearing it here is what makes
     the next assertion about *this* pass. */
  vm_obj_get_by_id(SET_ENO(1))->head.f.upd = 0;
  vm_exec_pass();

  ck("a stale source does not copy", near_f(set_f(SET_DST1), 1.0f) && !set_eno(1));
  ck("...with the ENO taken false quietly", vm_obj_get_by_id(SET_ENO(1))->head.f.upd == 0);

  /* Block 8's target is still fresh from its own pass-1 copy, and its source
     is not. A vm_block_triggered() over both pins would read the target as an
     arrival and copy again -- and since each copy refreshes the target, it
     would never stop. */
  ck("a fresh target does not re-fire a Set", near_f(set_f(SET_DST8), 2.0f) && !set_eno(8));
  ck("...and that really was source-stale, target-fresh",
     vm_obj_get_by_id(SET_SRC8)->head.f.upd == 0 && vm_obj_get_by_id(SET_DST8)->head.f.upd != 0);
}

/* ==========================================================================
   Stage X -- the Clone block

   The first block that allocates, and the first thing outside stage O to put
   vm_obj_dyn to work. Two properties carry the design, and everything here is
   arranged to check them:

     the steady state allocates nothing -- a destination that already matches
     is filled in place, so a Clone running at scan rate is a Set

     a shape change swaps the tree, and the tree it replaced is freed by the
     pointer slot that stopped naming it

   The second is only observable through the register, so most assertions here
   are counts of live dynamic objects across passes rather than values.
   ========================================================================== */

#define CLN_HOLD 0     // PTR[1] -- the source holder; its child is what gets cloned
#define CLN_A 1        // F[2], the first shape
#define CLN_B 2        // F[5], the second
#define CLN_CELL 3     // PTR[1] -- the cell the block owns its tree through
#define CLN_HOLD2 4    // PTR[1] -- holder for the 2D source
#define CLN_TBL 5      // PTR[2] source table
#define CLN_R0 6       // F[3] rows
#define CLN_R1 7
#define CLN_TCELL 8    // PTR[1] -- destination cell for the table
#define CLN_NOTCELL 9  // F[1] -- a target that is not a pointer cell at all

#define CLN_ENO(n) ((uint16_t)(20 + (n)))

static uint16_t dyn_live(void) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (vm_obj_dyn_get_by_id(i)) n++;
  }
  return n;
}

// the object the block built, reached the way a downstream block would
static vm_obj_h cln_built(uint16_t cell_obj) {
  vm_obj_h c = vm_obj_get_by_id(cell_obj);
  return c ? ((vm_obj_h*)c->payload)[0] : NULL;
}

/* source pin is `holder[0]`, so the *child* is what the block sees -- which is
   how this stage changes a source's shape between passes without an arena
   object ever changing shape. */
static vm_accessor_t* cln_child_acc(uint16_t acc_id, uint16_t holder) {
  vm_accessor_t* a = NULL;
  if (vm_accessor_create(&a, acc_id, holder, 1) != NULL) return NULL;
  if (vm_accessor_set_literal(a, 0, 0) != NULL) return NULL;
  (void)vm_accessor_cache_build(a);
  return a;
}

// same reason set_blk() takes one -- see there
static bool clone_blk(uint16_t id, const uint16_t* ins, uint8_t in_cnt, uint16_t eno) {
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_CLONE,
                                              .in_cnt = in_cnt,
                                              .q_cnt = 0,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = VM_CLONE_CUSTOM_LEN,
                                              .in_acc_ids = ins,
                                              .eno_obj_id = eno});
  return e == NULL && b != NULL;
}

static bool cln_eno(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(CLN_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

void test_clone(void) {
  ESP_LOGI(TAG, "-- X: the Clone block --");
  vm_obj_dyn_reset();  // before the loader: the parents holding these live in the pool
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 40, [VM_REG_ACC] = 16, [VM_REG_BLK] = 8};
  (void)vm_store_open(8192, counts);

  bool built = true;
  built = built && mk(CLN_HOLD, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(CLN_A, VM_OBJ_F, 2, "a", true) != NULL;
  built = built && mk(CLN_B, VM_OBJ_F, 5, NULL, true) != NULL;
  built = built && mk(CLN_CELL, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(CLN_HOLD2, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(CLN_TBL, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(CLN_R0, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(CLN_R1, VM_OBJ_F, 3, NULL, true) != NULL;
  built = built && mk(CLN_TCELL, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(CLN_NOTCELL, VM_OBJ_F, 1, NULL, true) != NULL;
  for (uint16_t n = 0; n <= 2; n++) built = built && mk(CLN_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  ck("objects built", built);

  bool wired = built;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(CLN_HOLD), 0, vm_obj_get_by_id(CLN_A)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(CLN_TBL), 0, vm_obj_get_by_id(CLN_R0)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(CLN_TBL), 1, vm_obj_get_by_id(CLN_R1)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(CLN_HOLD2), 0, vm_obj_get_by_id(CLN_TBL)) == NULL;
  ck("sources wired", wired);
  ck("linking arena objects took no dynamic references", dyn_live() == 0);

  ((float*)vm_obj_get_by_id(CLN_A)->payload)[0] = 1.0f;
  ((float*)vm_obj_get_by_id(CLN_A)->payload)[1] = 2.0f;
  for (uint8_t i = 0; i < 5; i++) ((float*)vm_obj_get_by_id(CLN_B)->payload)[i] = (float)(10 + i);
  for (uint8_t i = 0; i < 3; i++) {
    ((float*)vm_obj_get_by_id(CLN_R0)->payload)[i] = (float)(i + 1);
    ((float*)vm_obj_get_by_id(CLN_R1)->payload)[i] = (float)(i + 4);
  }
  /* Neither holder is upd_resetable, so both read fresh on every pass and the
     blocks fire every time -- the stand-down cases belong to stage W. */
  vm_obj_get_by_id(CLN_HOLD)->head.f.upd = 1;
  vm_obj_get_by_id(CLN_HOLD2)->head.f.upd = 1;

  bool accs = cln_child_acc(0, CLN_HOLD) != NULL;      // acc 0: the scalar source
  accs = accs && ex_acc(1, CLN_CELL) != NULL;          // acc 1: its destination cell
  accs = accs && cln_child_acc(2, CLN_HOLD2) != NULL;  // acc 2: the table source
  accs = accs && ex_acc(3, CLN_TCELL) != NULL;         // acc 3: its destination cell
  accs = accs && ex_acc(4, CLN_NOTCELL) != NULL;       // acc 4: not a cell

  /* How a downstream block reads what a Clone built. A dynamic object has no
     registry id -- nothing can be rooted at it -- so the accessor is rooted at
     the arena *cell* and steps through the pointer slot. Two spellings of the
     same reach: by position, and by the tag the clone carried over. */
  vm_accessor_t* a_read = NULL;
  accs = accs && vm_accessor_create(&a_read, 5, CLN_CELL, 2) == NULL;
  accs = accs && vm_accessor_set_literal(a_read, 0, 0) == NULL;
  accs = accs && vm_accessor_set_literal(a_read, 1, 1) == NULL;
  vm_accessor_t* a_named = NULL;
  accs = accs && vm_accessor_create(&a_named, 6, CLN_CELL, 2) == NULL;
  accs = accs && vm_accessor_set_name(a_named, 0, "a", 1) == NULL;
  accs = accs && vm_accessor_set_literal(a_named, 1, 1) == NULL;
  if (accs) {
    /* Neither may be cached, and cache_build is what refuses them: a chain
       that crosses a link has to re-read the slot every pass, or it would go
       on naming a tree the block has already replaced and freed. */
    (void)vm_accessor_cache_build(a_read);
    (void)vm_accessor_cache_build(a_named);
  }
  ck("accessors built", accs);
  ck("a chain that crosses a link is never cached",
     accs && !(a_read->flags & VM_ACC_F_CACHED) && !(a_named->flags & VM_ACC_F_CACHED));

  bool blk = true;
  blk = blk && clone_blk(0, (const uint16_t[]){0, 1}, 2, CLN_ENO(0));
  blk = blk && clone_blk(1, (const uint16_t[]){2, 3}, 2, CLN_ENO(1));
  blk = blk && clone_blk(2, (const uint16_t[]){0, 4}, 2, CLN_ENO(2));
  ck("Clone blocks built in execution order", blk);
  if (!blk) return;

  /* ---- pass 1: nothing exists yet, so both Clones build ---- */
  vm_exec_pass();

  vm_obj_h c1 = cln_built(CLN_CELL);
  ck("a Clone builds its destination and fills it", c1 && near_f(((const float*)c1->payload)[1], 2.0f) && cln_eno(0));
  ck("...on the heap, registered, and marked dynamic", c1 && vm_obj_is_dynamic(c1) && vm_obj_dyn_get_id(c1) != VM_DYN_NO_ID);
  ck("...shaped like its source, tag and all", c1 && vm_obj_get_items_cnt(c1) == 2 && c1->head.f.tagged && c1->head.d.name_size == 1);
  ck("...and mutable, whatever the source was", c1 && c1->head.f.mutable);

  /* A tree source clones as a tree: the rows are built too, and they are the
     copy's own rows, not the source's. */
  vm_obj_h t1 = cln_built(CLN_TCELL);
  ck("a 2D source clones row by row", t1 && vm_obj_get_items_cnt(t1) == 2 && cln_eno(1) &&
                                          near_f(((const float*)((vm_obj_h*)t1->payload)[1]->payload)[2], 6.0f));
  ck("...and the rows belong to the copy", t1 && ((vm_obj_h*)t1->payload)[0] != vm_obj_get_by_id(CLN_R0) &&
                                               vm_obj_is_dynamic(((vm_obj_h*)t1->payload)[0]));

  // 1 scalar + 1 table + 2 rows
  ck("the register holds exactly what was built", dyn_live() == 4);
  ck("a target that is not a pointer cell is refused", !cln_eno(2));

  /* Reading the clone the way a wired block would. */
  float rd = 0.0f;
  ck("a clone is read through the cell that holds it", VM_OBJ_GET_VAL(rd, a_read) == NULL && near_f(rd, 2.0f));
  rd = 0.0f;
  ck("...or by the tag it carried over from its source", VM_OBJ_GET_VAL(rd, a_named) == NULL && near_f(rd, 2.0f));
  ck("the cell itself still reads as 0 -- a pointer is not a value",
     VM_OBJ_GET_VAL(rd, vm_accessor_get_by_id(1)) == NULL && near_f(rd, 0.0f));

  /* ---- pass 2: same shapes, new values. The whole point -- nothing new is
          allocated and the destination objects are the same ones. ---- */
  ((float*)vm_obj_get_by_id(CLN_A)->payload)[1] = 42.0f;
  vm_exec_pass();

  ck("a matching destination is refilled, not rebuilt", cln_built(CLN_CELL) == c1 && near_f(((const float*)c1->payload)[1], 42.0f));
  ck("...so a Clone at scan rate allocates nothing", dyn_live() == 4);
  ck("the table is likewise reused", cln_built(CLN_TCELL) == t1);

  /* ---- pass 3: the source changes shape under the block ---- */
  ck("re-point the holder at a differently shaped child", vm_obj_link_direct(vm_obj_get_by_id(CLN_HOLD), 0, vm_obj_get_by_id(CLN_B)) == NULL);
  vm_obj_get_by_id(CLN_HOLD)->head.f.upd = 1;
  vm_exec_pass();

  vm_obj_h c2 = cln_built(CLN_CELL);
  ck("a shape change builds a new destination", c2 && c2 != c1 && vm_obj_get_items_cnt(c2) == 5);
  ck("...filled from the new source", c2 && near_f(((const float*)c2->payload)[4], 14.0f) && cln_eno(0));
  /* The old tree was freed by the slot that stopped naming it -- if the link
     path did not move the reference, this count would climb every time a
     source changed shape, which is the leak the refcount exists to prevent. */
  ck("...and the tree it replaced was released", dyn_live() == 4);

  /* Independence, the same property stage W checks for Set: the clone holds
     its own storage, so editing it cannot reach back into the source. */
  ((float*)c2->payload)[0] = -1.0f;
  ck("editing the clone does not reach the source", near_f(((const float*)vm_obj_get_by_id(CLN_B)->payload)[0], 10.0f));

  /* The reason such a chain must not be cached: the tree it reached through
     was freed two statements into this pass, and the same accessor now has to
     find the one that replaced it. */
  rd = 0.0f;
  ck("a reader follows the swap without being rebuilt", VM_OBJ_GET_VAL(rd, a_read) == NULL && near_f(rd, 11.0f));
  /* CLN_B carries no tag, so the clone of it carries none either -- the
     by-name reach stops resolving rather than quietly finding something else. */
  ck("...while a name the new tree does not have stops resolving", VM_OBJ_GET_VAL(rd, a_named) != NULL);

  /* Teardown is the loader's job, and it has to happen before the pool goes:
     every one of these is held by an arena cell. */
  vm_obj_dyn_reset();
  ck("reset frees every live clone", dyn_live() == 0);
}

/* ==========================================================================
   Stage Y -- read a message, compute on the copy, write back into the copy

   The pipeline the palette exists for, end to end and in one pass:

     CLONE   takes a private copy of an arriving tree
     EXPR    reads a field of that copy and computes
     SET     writes the answer into another field of the same copy

   Everything here is about one property: the original is never touched. A
   program that mutated the message it was handed would be publishing changes
   to whatever else that message is wired to, and the copy exists so it does
   not have to.

   The message is an arena tree rather than a parsed one, because a parser is
   not the subject -- a Clone cannot tell the difference, and this way the
   stage is about the wiring.
   ========================================================================== */

#define JS_CELL 0   // PTR[1] -- where a message arrives
#define JS_ROOT 1   // PTR[2] -- its root; the fields below are its tagged children
#define JS_TEMP 2   // F "temp"   -- the field read
#define JS_RES 3    // F "result" -- the field written
#define JS_COPY 4   // PTR[1] -- the cell the Clone owns its copy through
#define JS_MATH 5   // F -- the expression's own output, an ordinary arena object

#define JS_ENO(n) ((uint16_t)(10 + (n)))

// the field `tag` of the copy, reached the way a wired block reaches it
static vm_obj_h js_field(const char* tag) {
  vm_obj_h cell = vm_obj_get_by_id(JS_COPY);
  vm_obj_h copy = cell ? ((vm_obj_h*)cell->payload)[0] : NULL;
  return copy ? vm_obj_get_child(copy, tag) : NULL;
}

static float js_field_f(const char* tag) {
  vm_obj_h f = js_field(tag);
  return f ? *(const float*)f->payload : -1.0f;
}

static bool js_eno(uint16_t n) {
  vm_obj_h o = vm_obj_get_by_id(JS_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

/* copy_cell[0]["<tag>"][0] -- three steps, and every one of them earns its
   place. [0] crosses the pointer slot into the copy; ["tag"] finds the field
   by the name the clone carried over; [0] lands on the value inside it. */
static bool js_field_acc(uint16_t acc_id, const char* tag) {
  vm_accessor_t* a = NULL;
  if (vm_accessor_create(&a, acc_id, JS_COPY, 3) != NULL) return false;
  if (vm_accessor_set_literal(a, 0, 0) != NULL) return false;
  if (vm_accessor_set_name(a, 1, tag, (uint8_t)strlen(tag)) != NULL) return false;
  if (vm_accessor_set_literal(a, 2, 0) != NULL) return false;
  (void)vm_accessor_cache_build(a);  // declines: the chain crosses a link
  return true;
}

void test_json_pipeline(void) {
  ESP_LOGI(TAG, "-- Y: clone, compute, write back --");
  vm_obj_dyn_reset();
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 24, [VM_REG_ACC] = 8, [VM_REG_BLK] = 4};
  (void)vm_store_open(8192, counts);

  bool built = true;
  built = built && mk(JS_CELL, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(JS_ROOT, VM_OBJ_PTR, 2, NULL, true) != NULL;
  built = built && mk(JS_TEMP, VM_OBJ_F, 1, "temp", true) != NULL;
  built = built && mk(JS_RES, VM_OBJ_F, 1, "result", true) != NULL;
  built = built && mk(JS_COPY, VM_OBJ_PTR, 1, NULL, true) != NULL;
  built = built && mk(JS_MATH, VM_OBJ_F, 1, NULL, true) != NULL;
  for (uint16_t n = 0; n <= 2; n++) built = built && mk(JS_ENO(n), VM_OBJ_B, 1, NULL, true) != NULL;
  ck("objects built", built);

  bool wired = built;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(JS_ROOT), 0, vm_obj_get_by_id(JS_TEMP)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(JS_ROOT), 1, vm_obj_get_by_id(JS_RES)) == NULL;
  wired = wired && vm_obj_link_direct(vm_obj_get_by_id(JS_CELL), 0, vm_obj_get_by_id(JS_ROOT)) == NULL;
  ck("message wired", wired);

  *(float*)vm_obj_get_by_id(JS_TEMP)->payload = 21.5f;
  *(float*)vm_obj_get_by_id(JS_RES)->payload = 0.0f;

  /* Both of these have to be resetable or the chain never stands down: the
     cell would read as a message arriving on every pass, and the expression's
     output as a fresh answer forever, so the last assertions in this stage
     would be measuring a program that cannot stop rather than one that has.
     The clone's own objects get the flag from clone_shape(). */
  vm_obj_get_by_id(JS_CELL)->head.f.upd_resetable = 1;
  vm_obj_get_by_id(JS_MATH)->head.f.upd_resetable = 1;
  vm_obj_get_by_id(JS_CELL)->head.f.upd = 1;  // a message arrived

  bool accs = true;
  vm_accessor_t* a_msg = NULL;
  accs = accs && vm_accessor_create(&a_msg, 0, JS_CELL, 1) == NULL;  // the tree behind the cell
  accs = accs && vm_accessor_set_literal(a_msg, 0, 0) == NULL;
  accs = accs && ex_acc(1, JS_COPY) != NULL;   // the Clone's destination cell
  accs = accs && js_field_acc(2, "temp");      // what the expression reads
  accs = accs && ex_acc(3, JS_MATH) != NULL;   // what it writes
  accs = accs && js_field_acc(4, "result");    // where the answer lands
  ck("accessors built", accs);

  /* temp * 2. The output object is an ordinary arena F -- an expression cannot
     publish into the copy directly, because a block output is a raw handle
     bound at load and a clone does not exist then. Getting the answer into the
     copy is the Set's job, one block further on. */
  static const uint8_t c_double[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_MUL};
  const uint32_t k2[] = {kf(2.0f)};

  bool blk = true;
  blk = blk && clone_blk(0, (const uint16_t[]){0, 1}, 2, JS_ENO(0));
  blk = blk && ex_expr(1, (const uint16_t[]){2}, 1, JS_MATH, JS_ENO(1), k2, 1, c_double, sizeof(c_double));
  blk = blk && set_blk(2, (const uint16_t[]){3, 4}, 2, VM_BLOCK_NO_ID, JS_ENO(2));
  ck("clone -> expr -> set built in execution order", blk);
  if (!blk) return;

  /* ---- one pass carries the whole chain: each block's write is the next
          block's arrival, and topological order is what makes that hold ---- */
  vm_exec_pass();

  ck("the clone took the message's shape and tags", js_field("temp") && js_field("result"));
  ck("...and its values", near_f(js_field_f("temp"), 21.5f) && js_eno(0));
  ck("the expression read the copy through three steps", near_f(*(const float*)vm_obj_get_by_id(JS_MATH)->payload, 43.0f) && js_eno(1));
  ck("the Set wrote the answer into the copy", near_f(js_field_f("result"), 43.0f) && js_eno(2));

  /* The point of the copy. Everything above happened to the clone, and the
     message a client sent is byte-for-byte what it sent. */
  ck("the original message is untouched", near_f(*(const float*)vm_obj_get_by_id(JS_RES)->payload, 0.0f));
  ck("...and its fields are still its own objects", js_field("result") != vm_obj_get_by_id(JS_RES));

  /* ---- a second message, same shape: the copy is refilled rather than
          rebuilt, so a message rate is not an allocation rate ---- */
  vm_obj_h first = ((vm_obj_h*)vm_obj_get_by_id(JS_COPY)->payload)[0];
  uint16_t live = dyn_live();
  *(float*)vm_obj_get_by_id(JS_TEMP)->payload = 10.0f;
  vm_obj_get_by_id(JS_CELL)->head.f.upd = 1;
  vm_exec_pass();

  ck("a second message reuses the copy", ((vm_obj_h*)vm_obj_get_by_id(JS_COPY)->payload)[0] == first && dyn_live() == live);
  ck("...and flows through to the answer", near_f(js_field_f("result"), 20.0f) && js_eno(2));

  /* ---- nothing arrives: the chain stands down from the top ---- */
  vm_exec_pass();
  ck("no message means no clone, so nothing downstream runs", !js_eno(0) && !js_eno(1) && !js_eno(2));
  ck("...and the last answer stands", near_f(js_field_f("result"), 20.0f));

  vm_obj_dyn_reset();
}

/* ==========================================================================
   Selection pipeline: Modulo -> Switch -> Gated Actions + Telemetry
   ========================================================================== */
#define PIPE_O_X 0
#define PIPE_O_MOD 1
#define PIPE_O_Q0 2
#define PIPE_O_Q1 3
#define PIPE_O_Q2 4
#define PIPE_O_ENO_MOD 5
#define PIPE_O_ENO_SW 6
#define PIPE_O_ENO_INC 7
#define PIPE_O_ENO_DBL 8

#define PIPE_ACC_X 0
#define PIPE_ACC_MOD 1
#define PIPE_ACC_Q0 2
#define PIPE_ACC_Q2 3

#define PIPE_BLK_MOD 0
#define PIPE_BLK_SW 1
#define PIPE_BLK_INC 2
#define PIPE_BLK_DBL 3

static uint8_t s_pipe_sub_buf[256];
static size_t s_pipe_sub_len;
static int s_pipe_sub_calls;

static err_h pipe_mock_sender(const uint8_t* data, size_t len) {
  s_pipe_sub_calls++;
  s_pipe_sub_len = (len < sizeof(s_pipe_sub_buf)) ? len : sizeof(s_pipe_sub_buf);
  memcpy(s_pipe_sub_buf, data, s_pipe_sub_len);
  return NULL;
}

static bool ex_expr_gated(uint16_t id, const uint16_t* ins, uint8_t in_cnt, uint16_t en_acc,
                          uint16_t out_obj, uint16_t eno,
                          const uint32_t* ks, uint8_t k_cnt, const uint8_t* code, uint16_t code_len) {
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_EXPR,
                                              .in_cnt = in_cnt,
                                              .q_cnt = 1,
                                              .en_cnt = (en_acc != VM_BLOCK_NO_ID) ? 1 : 0,
                                              .en_mode = VM_BLK_EN_ALL,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = (uint16_t)vm_expr_size(k_cnt, code_len),
                                              .in_acc_ids = ins,
                                              .out_obj_ids = (const uint16_t[]){out_obj},
                                              .en_acc_ids = (en_acc != VM_BLOCK_NO_ID) ? (const uint16_t[]){en_acc} : NULL,
                                              .eno_obj_id = eno});
  if (e != NULL || b == NULL) return false;

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  c->const_cnt = k_cnt;
  c->code_len = code_len;
  for (uint8_t i = 0; i < k_cnt; i++) c->consts[i].u = ks[i];
  memcpy(&c->consts[k_cnt], code, code_len);
  return true;
}

static bool ex_switch_3(uint16_t id, uint16_t in_acc, const uint16_t* outs, uint16_t eno) {
  vm_block_h b = NULL;
  err_h e = vm_block_create(&b, id,
                            &(vm_block_cfg_t){.block_idx = id,
                                              .block_type = VM_BLK_SWITCH,
                                              .in_cnt = 1,
                                              .q_cnt = 3,
                                              .en_cnt = 0,
                                              .on_error = VM_BLK_ERR_STOP,
                                              .custom_len = VM_BRANCH_CUSTOM_LEN,
                                              .in_acc_ids = (const uint16_t[]){in_acc},
                                              .out_obj_ids = outs,
                                              .eno_obj_id = eno});
  return e == NULL && b != NULL;
}

void test_step_selection_pipeline(void) {
  ESP_LOGI(TAG, "-- Selection pipeline: step-by-step & telemetry --");

  vm_loader_reset();
  vm_sub_reset();
  (void)vm_sub_init();
  vm_sub_set_sender(pipe_mock_sender);
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 16, [VM_REG_ACC] = 8, [VM_REG_BLK] = 4};
  ck("pipeline arena opens", vm_store_open(DIRECT_POOL, counts) == NULL);

  // Objects
  vm_obj_head_t hx = hd(VM_OBJ_F, 1);
  hx.f.mutable = 1;
  hx.f.upd_resetable = 1;
  vm_obj_h ox = NULL;
  bool built = vm_obj_create(&ox, PIPE_O_X, &hx, "x") == NULL;

  vm_obj_head_t hm = hd(VM_OBJ_F, 1);
  hm.f.mutable = 1;
  hm.f.upd_resetable = 1;
  vm_obj_h om = NULL;
  built = built && vm_obj_create(&om, PIPE_O_MOD, &hm, "mod") == NULL;

  built = built && mk(PIPE_O_Q0, VM_OBJ_B, 1, "q0", true) != NULL;
  built = built && mk(PIPE_O_Q1, VM_OBJ_B, 1, "q1", true) != NULL;
  built = built && mk(PIPE_O_Q2, VM_OBJ_B, 1, "q2", true) != NULL;

  for (uint16_t i = PIPE_O_ENO_MOD; i <= PIPE_O_ENO_DBL; i++) {
    built = built && mk(i, VM_OBJ_B, 1, NULL, true) != NULL;
  }
  ck("pipeline objects built", built);
  if (!built) return;

  // Accessors
  bool accs = true;
  accs = accs && ex_acc(PIPE_ACC_X, PIPE_O_X) != NULL;
  accs = accs && ex_acc(PIPE_ACC_MOD, PIPE_O_MOD) != NULL;
  accs = accs && ex_acc(PIPE_ACC_Q0, PIPE_O_Q0) != NULL;
  accs = accs && ex_acc(PIPE_ACC_Q2, PIPE_O_Q2) != NULL;
  ck("pipeline accessors built", accs);
  if (!accs) return;

  // Bytecode definitions
  static const uint8_t c_mod3[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_MOD};
  const uint32_t k3[] = {kf(3.0f)};
  static const uint8_t c_add1[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_ADD};
  const uint32_t k1[] = {kf(1.0f)};
  static const uint8_t c_mul2[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_MUL};
  const uint32_t k2[] = {kf(2.0f)};

  // Blocks
  bool blks = true;
  blks = blks && ex_expr_gated(PIPE_BLK_MOD, (const uint16_t[]){PIPE_ACC_X}, 1, VM_BLOCK_NO_ID,
                               PIPE_O_MOD, PIPE_O_ENO_MOD, k3, 1, c_mod3, sizeof(c_mod3));
  blks = blks && ex_switch_3(PIPE_BLK_SW, PIPE_ACC_MOD,
                             (const uint16_t[]){PIPE_O_Q0, PIPE_O_Q1, PIPE_O_Q2}, PIPE_O_ENO_SW);
  blks = blks && ex_expr_gated(PIPE_BLK_INC, (const uint16_t[]){PIPE_ACC_X}, 1, PIPE_ACC_Q0,
                               PIPE_O_X, PIPE_O_ENO_INC, k1, 1, c_add1, sizeof(c_add1));
  blks = blks && ex_expr_gated(PIPE_BLK_DBL, (const uint16_t[]){PIPE_ACC_X}, 1, PIPE_ACC_Q2,
                               PIPE_O_X, PIPE_O_ENO_DBL, k2, 1, c_mul2, sizeof(c_mul2));
  ck("pipeline blocks built in execution order", blks);
  if (!blks) return;

  // Subscribe to x (OBJ ID 0)
  const uint16_t sub_ids[] = {PIPE_O_X};
  ck("pipeline: subscribe to x", vm_sub_subscribe(sub_ids, 1) == NULL && vm_sub_count() == 1);

  // Set block mode
  ck("pipeline: select block mode", vm_exec_control(VM_EXEC_BLOCK_MODE) == NULL);

  /* =========================================================================
     Pass 1: x = 0.0f -> mod = 0 -> Q0 active -> x + 1 => x = 1.0f
     ========================================================================= */
  *(float*)vm_obj_get_by_id(PIPE_O_X)->payload = 0.0f;
  vm_obj_get_by_id(PIPE_O_X)->head.f.upd = 1;
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  // Queue first block dispatch (block 0) and start worker
  ck("p1: queue first block next", vm_exec_control(VM_EXEC_NEXT) == NULL);
  __atomic_store_n(&s_step_test_done, false, __ATOMIC_RELEASE);
  bool started = xTaskCreate(ex_step_worker, "vm_pipe_step", 4096, NULL, 5, NULL) == pdPASS;
  ck("p1: step worker started", started);
  if (!started) { vm_exec_stop(); return; }

  // Worker runs block 0 (expr mod) and holds before block 1 (switch)
  bool held = ex_wait_block(PIPE_BLK_SW);
  ck("p1: block 0 evaluated mod=0, holds before switch", held && near_f(ex_f(PIPE_O_MOD), 0.0f));
  ck("p1: switch outputs not driven yet", !ex_b(PIPE_O_Q0) && !ex_b(PIPE_O_Q1) && !ex_b(PIPE_O_Q2));

  // Step block 1 (switch) -> holds before block 2 (expr x+1)
  ck("p1: step switch", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_INC);
  ck("p1: switch branch 0 taken (Q0=1, Q1=0, Q2=0)", held && ex_b(PIPE_O_Q0) && !ex_b(PIPE_O_Q1) && !ex_b(PIPE_O_Q2));
  ck("p1: x still 0 before block 2 runs", near_f(ex_f(PIPE_O_X), 0.0f));

  // Step block 2 (expr x+1) -> holds before block 3 (expr x*2)
  ck("p1: step block 2 (x+1)", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_DBL);
  ck("p1: block 2 executed, x=1.0f", held && near_f(ex_f(PIPE_O_X), 1.0f));

  // Step block 3 (expr x*2, gated by Q2 which is 0) -> scan finishes
  ck("p1: step block 3", vm_exec_control(VM_EXEC_NEXT) == NULL);
  ck("p1: scan 1 completed", ex_wait_done() && vm_exec_pass_count() == 1);
  ck("p1: x remains 1.0f (block 3 skipped)", near_f(ex_f(PIPE_O_X), 1.0f));
  ck("p1: telemetry emitted for x", s_pipe_sub_calls == 1);

  float rx = 0.0f;
  if (s_pipe_sub_len >= 13) {
    memcpy(&rx, s_pipe_sub_buf + 9, 4);
  }
  ck("p1: telemetry value is 1.0f", near_f(rx, 1.0f));

  /* =========================================================================
     Pass 2: Idle pass without updates -> telemetry is NOT emitted
     ========================================================================= */
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  ck("p2: queue next", vm_exec_control(VM_EXEC_NEXT) == NULL);
  __atomic_store_n(&s_step_test_done, false, __ATOMIC_RELEASE);
  started = xTaskCreate(ex_step_worker, "vm_pipe_step", 4096, NULL, 5, NULL) == pdPASS;
  ck("p2: worker started", started);
  if (!started) { vm_exec_stop(); return; }

  // Because x has upd=0, block 0 is not triggered -> sweeps through to completion
  held = ex_wait_block(PIPE_BLK_SW);
  ck("p2: block 0 held before switch", held);
  ck("p2: step switch", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_INC);
  ck("p2: held before block 2", held);
  ck("p2: step block 2", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_DBL);
  ck("p2: held before block 3", held);
  ck("p2: step block 3", vm_exec_control(VM_EXEC_NEXT) == NULL);
  ck("p2: scan 2 completed", ex_wait_done() && vm_exec_pass_count() == 2);
  ck("p2: no telemetry emitted when x is untouched", s_pipe_sub_calls == 0);
  ck("p2: x unchanged", near_f(ex_f(PIPE_O_X), 1.0f));

  /* =========================================================================
     Pass 3: x set to 2.0f -> mod = 2 -> Q2 active -> block x*2 runs => x = 4.0f
     ========================================================================= */
  *(float*)vm_obj_get_by_id(PIPE_O_X)->payload = 2.0f;
  vm_obj_get_by_id(PIPE_O_X)->head.f.upd = 1;
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  ck("p3: queue next", vm_exec_control(VM_EXEC_NEXT) == NULL);
  __atomic_store_n(&s_step_test_done, false, __ATOMIC_RELEASE);
  started = xTaskCreate(ex_step_worker, "vm_pipe_step", 4096, NULL, 5, NULL) == pdPASS;
  ck("p3: worker started", started);
  if (!started) { vm_exec_stop(); return; }

  held = ex_wait_block(PIPE_BLK_SW);
  ck("p3: block 0 evaluated mod=2", held && near_f(ex_f(PIPE_O_MOD), 2.0f));

  ck("p3: step switch", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_INC);
  ck("p3: switch branch 2 taken (Q0=0, Q1=0, Q2=1)", held && !ex_b(PIPE_O_Q0) && !ex_b(PIPE_O_Q1) && ex_b(PIPE_O_Q2));

  // Step block 2 (disabled because Q0=0)
  ck("p3: step block 2", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PIPE_BLK_DBL);
  ck("p3: block 2 skipped, x still 2.0f", held && near_f(ex_f(PIPE_O_X), 2.0f));

  // Step block 3 (enabled because Q2=1) -> computes 2.0 * 2.0 = 4.0f
  ck("p3: step block 3 (x*2)", vm_exec_control(VM_EXEC_NEXT) == NULL);
  ck("p3: scan 3 completed", ex_wait_done() && vm_exec_pass_count() == 3);
  ck("p3: block 3 executed, x=4.0f", near_f(ex_f(PIPE_O_X), 4.0f));
  ck("p3: telemetry emitted for x", s_pipe_sub_calls == 1);

  rx = 0.0f;
  if (s_pipe_sub_len >= 13) {
    memcpy(&rx, s_pipe_sub_buf + 9, 4);
  }
  ck("p3: telemetry value is 4.0f", near_f(rx, 4.0f));

  // Teardown
  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);
  vm_sub_reset();
  vm_loader_reset();
}

/* ==========================================================================
   Mathematical Test: Nilakantha Series for Pi
   ========================================================================== */
#define PI_O_PI 0
#define PI_O_D 1
#define PI_O_ENO_FOR 2
#define PI_O_ENO_EXPR 3

#define PI_ACC_PI 0
#define PI_ACC_D 1

#define PI_BLK_FOR 0
#define PI_BLK_CALC 1

void test_math_pi(void) {
  ESP_LOGI(TAG, "-- Mathematical test: Nilakantha Pi calculation --");

  vm_loader_reset();
  vm_sub_reset();
  (void)vm_sub_init();
  vm_sub_set_sender(pipe_mock_sender);
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 8, [VM_REG_ACC] = 4, [VM_REG_BLK] = 4};
  ck("pi arena opens", vm_store_open(DIRECT_POOL, counts) == NULL);

  // Objects
  vm_obj_head_t hp = hd(VM_OBJ_F, 1);
  hp.f.mutable = 1;
  hp.f.upd_resetable = 1;
  vm_obj_h opi = NULL;
  bool built = vm_obj_create(&opi, PI_O_PI, &hp, "pi") == NULL;

  vm_obj_head_t hd_obj = hd(VM_OBJ_F, 1);
  hd_obj.f.mutable = 1;
  hd_obj.f.upd_resetable = 1;
  vm_obj_h od = NULL;
  built = built && vm_obj_create(&od, PI_O_D, &hd_obj, "d") == NULL;

  built = built && mk(PI_O_ENO_FOR, VM_OBJ_B, 1, NULL, true) != NULL;
  built = built && mk(PI_O_ENO_EXPR, VM_OBJ_B, 1, NULL, true) != NULL;
  ck("pi objects built", built);
  if (!built) return;

  // Accessors
  bool accs = true;
  accs = accs && ex_acc(PI_ACC_PI, PI_O_PI) != NULL;
  accs = accs && ex_acc(PI_ACC_D, PI_O_D) != NULL;
  ck("pi accessors built", accs);
  if (!accs) return;

  // Loop config: d from 2.0 to 46.0 by 4.0 (12 turns = 24 series terms)
  const for_loop_t lp = {
      .start = 2.0f,
      .end = 46.0f,
      .step = 4.0f,
      .budget = 64,
      .op = VM_FOR_OP_ADD,
      .cmp = VM_FOR_CMP_LE,
  };

  /* Nilakantha pair:
     pi = pi + 4.0 / (d * (d + 1) * (d + 2)) - 4.0 / ((d + 2) * (d + 3) * (d + 4)) */
  static const uint8_t c_pi_calc[] = {
      VM_EXPR_IN, 0,
      VM_EXPR_K, 0,
      VM_EXPR_IN, 1,
      VM_EXPR_IN, 1, VM_EXPR_K, 1, VM_EXPR_ADD,
      VM_EXPR_MUL,
      VM_EXPR_IN, 1, VM_EXPR_K, 2, VM_EXPR_ADD,
      VM_EXPR_MUL,
      VM_EXPR_DIV,
      VM_EXPR_ADD,
      VM_EXPR_K, 0,
      VM_EXPR_IN, 1, VM_EXPR_K, 2, VM_EXPR_ADD,
      VM_EXPR_IN, 1, VM_EXPR_K, 3, VM_EXPR_ADD,
      VM_EXPR_MUL,
      VM_EXPR_IN, 1, VM_EXPR_K, 0, VM_EXPR_ADD,
      VM_EXPR_MUL,
      VM_EXPR_DIV,
      VM_EXPR_SUB
  };
  const uint32_t pi_ks[] = {kf(4.0f), kf(1.0f), kf(2.0f), kf(3.0f)};

  // Blocks: Block 0 (FOR) owns Block 1 (EXPR)
  bool blks = true;
  blks = blks && ex_for(PI_BLK_FOR, 1, 2, lp, NULL, 0, (const uint16_t[]){PI_O_D}, 1, NULL, 0, PI_O_ENO_FOR,
                        sizeof(vm_for_code_t));
  blks = blks && ex_expr(PI_BLK_CALC, (const uint16_t[]){PI_ACC_PI, PI_ACC_D}, 2, PI_O_PI, PI_O_ENO_EXPR,
                         pi_ks, 4, c_pi_calc, sizeof(c_pi_calc));
  ck("pi blocks built", blks);
  if (!blks) return;

  // Subscribe to pi
  ck("pi subscribe", vm_sub_subscribe((const uint16_t[]){PI_O_PI}, 1) == NULL);

  // Initialize pi = 3.0f
  *(float*)vm_obj_get_by_id(PI_O_PI)->payload = 3.0f;
  vm_obj_get_by_id(PI_O_PI)->head.f.upd = 1;

  // Run pass: 12 loop turns execute within one scan
  vm_exec_pass();

  float pi_res = ex_f(PI_O_PI);
  ck("pi series completed", pi_res > 3.14f && pi_res < 3.15f);
  ck("pi accurate to 4 decimal places (within 0.0001 of pi)", fabsf(pi_res - 3.14159265f) < 1e-4f);
  ck("pi telemetry emitted", s_pipe_sub_calls == 1);

  float rx = 0.0f;
  if (s_pipe_sub_len >= 13) memcpy(&rx, s_pipe_sub_buf + 9, 4);
  ck("pi telemetry payload matches computed pi", fabsf(rx - pi_res) < 1e-5f);

  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);
  vm_sub_reset();
  vm_loader_reset();
}

/* ==========================================================================
   Mathematical Test: Prime Number Tester (Trial Division in FOR Loop)
   ========================================================================== */
#define PR_O_N 0
#define PR_O_END 1
#define PR_O_D 2
#define PR_O_IS_DIV 3
#define PR_O_DIV_CNT 4
#define PR_O_IS_PRIME 5
#define PR_O_ENO_SUB 6
#define PR_O_ENO_FOR 7
#define PR_O_ENO_MOD 8
#define PR_O_ENO_ADD 9
#define PR_O_ENO_CHK 10

#define PR_ACC_N 0
#define PR_ACC_END 1
#define PR_ACC_D 2
#define PR_ACC_IS_DIV 3
#define PR_ACC_DIV_CNT 4

#define PR_BLK_SUB 0
#define PR_BLK_FOR 1
#define PR_BLK_MOD 2
#define PR_BLK_ADD 3
#define PR_BLK_CHK 4

void test_math_primes(void) {
  ESP_LOGI(TAG, "-- Mathematical test: Prime number tester --");

  vm_loader_reset();
  vm_sub_reset();
  (void)vm_sub_init();
  vm_sub_set_sender(pipe_mock_sender);
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 16, [VM_REG_ACC] = 8, [VM_REG_BLK] = 8};
  ck("prime arena opens", vm_store_open(DIRECT_POOL, counts) == NULL);

  // Objects
  bool built = true;
  for (uint16_t i = PR_O_N; i <= PR_O_IS_PRIME; i++) {
    vm_obj_head_t h = hd(VM_OBJ_F, 1);
    h.f.mutable = 1;
    h.f.upd_resetable = 1;
    vm_obj_h o = NULL;
    built = built && vm_obj_create(&o, i, &h, NULL) == NULL;
  }
  for (uint16_t i = PR_O_ENO_SUB; i <= PR_O_ENO_CHK; i++) {
    built = built && mk(i, VM_OBJ_B, 1, NULL, true) != NULL;
  }
  ck("prime objects built", built);
  if (!built) return;

  // Accessors
  bool accs = true;
  accs = accs && ex_acc(PR_ACC_N, PR_O_N) != NULL;
  accs = accs && ex_acc(PR_ACC_END, PR_O_END) != NULL;
  accs = accs && ex_acc(PR_ACC_D, PR_O_D) != NULL;
  accs = accs && ex_acc(PR_ACC_IS_DIV, PR_O_IS_DIV) != NULL;
  accs = accs && ex_acc(PR_ACC_DIV_CNT, PR_O_DIV_CNT) != NULL;
  ck("prime accessors built", accs);
  if (!accs) return;

  // Bytecodes
  // Block 0: end = N - 1.0f
  static const uint8_t c_sub1[] = {VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_SUB};
  const uint32_t k_one[] = {kf(1.0f)};

  // Block 1: FOR d = 2.0 to end step 1.0 (owns blocks 2 and 3)
  const for_loop_t lp_div = {
      .start = 2.0f,
      .end = 2.0f,
      .step = 1.0f,
      .budget = 64,
      .op = VM_FOR_OP_ADD,
      .cmp = VM_FOR_CMP_LE,
  };

  // Block 2: is_div = ((N % d) == 0.0f) ? 1.0f : 0.0f
  static const uint8_t c_mod_chk[] = {
      VM_EXPR_IN, 0,
      VM_EXPR_IN, 1,
      VM_EXPR_MOD,
      VM_EXPR_K, 0,
      VM_EXPR_EQ
  };
  const uint32_t k_zero[] = {kf(0.0f)};

  // Block 3: div_cnt = div_cnt + is_div
  static const uint8_t c_add_div[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_ADD};

  // Block 4: is_prime = (div_cnt == 0.0f) && (N >= 2.0f)
  static const uint8_t c_prime_chk[] = {
      VM_EXPR_IN, 0, VM_EXPR_K, 0, VM_EXPR_EQ,
      VM_EXPR_IN, 1, VM_EXPR_K, 1, VM_EXPR_GE,
      VM_EXPR_AND
  };
  const uint32_t k_chk[] = {kf(0.0f), kf(2.0f)};

  // Blocks
  bool blks = true;
  blks = blks && ex_expr(PR_BLK_SUB, (const uint16_t[]){PR_ACC_N}, 1, PR_O_END, PR_O_ENO_SUB,
                         k_one, 1, c_sub1, sizeof(c_sub1));
  blks = blks && ex_for(PR_BLK_FOR, 2, 4, lp_div, (const uint16_t[]){VM_BLOCK_NO_ID, PR_ACC_END}, 2,
                        (const uint16_t[]){PR_O_D}, 1, NULL, 0, PR_O_ENO_FOR, sizeof(vm_for_code_t));
  blks = blks && ex_expr(PR_BLK_MOD, (const uint16_t[]){PR_ACC_N, PR_ACC_D}, 2, PR_O_IS_DIV, PR_O_ENO_MOD,
                         k_zero, 1, c_mod_chk, sizeof(c_mod_chk));
  blks = blks && ex_expr(PR_BLK_ADD, (const uint16_t[]){PR_ACC_DIV_CNT, PR_ACC_IS_DIV}, 2, PR_O_DIV_CNT, PR_O_ENO_ADD,
                         NULL, 0, c_add_div, sizeof(c_add_div));
  blks = blks && ex_expr(PR_BLK_CHK, (const uint16_t[]){PR_ACC_DIV_CNT, PR_ACC_N}, 2, PR_O_IS_PRIME, PR_O_ENO_CHK,
                         k_chk, 2, c_prime_chk, sizeof(c_prime_chk));
  ck("prime blocks built", blks);
  if (!blks) return;

  // Subscribe to is_prime
  ck("prime subscribe", vm_sub_subscribe((const uint16_t[]){PR_O_IS_PRIME}, 1) == NULL);

  // Test candidate numbers
  struct {
    float n;
    bool expect_prime;
    float expect_div_cnt;
  } test_cases[] = {
      {7.0f, true, 0.0f},
      {9.0f, false, 1.0f},   // 3
      {13.0f, true, 0.0f},
      {15.0f, false, 2.0f},  // 3, 5
      {2.0f, true, 0.0f},    // smallest prime
      {1.0f, false, 0.0f},   // not prime
      {29.0f, true, 0.0f},
  };

  for (unsigned idx = 0; idx < sizeof(test_cases) / sizeof(test_cases[0]); idx++) {
    float candidate = test_cases[idx].n;
    bool is_p = test_cases[idx].expect_prime;

    *(float*)vm_obj_get_by_id(PR_O_N)->payload = candidate;
    vm_obj_get_by_id(PR_O_N)->head.f.upd = 1;
    *(float*)vm_obj_get_by_id(PR_O_DIV_CNT)->payload = 0.0f;
    vm_obj_get_by_id(PR_O_DIV_CNT)->head.f.upd = 1;
    s_pipe_sub_calls = 0;

    vm_exec_pass();

    float res = ex_f(PR_O_IS_PRIME);
    float div_res = ex_f(PR_O_DIV_CNT);
    ck(is_p ? "candidate identified as prime" : "candidate identified as composite",
       (res != 0.0f) == is_p && near_f(div_res, test_cases[idx].expect_div_cnt));
    ck("prime telemetry emitted", s_pipe_sub_calls == 1);
  }

  /* Step-by-step block-mode verification for N = 7 */
  ck("select block mode for prime stepping", vm_exec_control(VM_EXEC_BLOCK_MODE) == NULL);
  *(float*)vm_obj_get_by_id(PR_O_N)->payload = 7.0f;
  vm_obj_get_by_id(PR_O_N)->head.f.upd = 1;
  *(float*)vm_obj_get_by_id(PR_O_DIV_CNT)->payload = 0.0f;
  vm_obj_get_by_id(PR_O_DIV_CNT)->head.f.upd = 1;

  ck("queue first step", vm_exec_control(VM_EXEC_NEXT) == NULL);
  __atomic_store_n(&s_step_test_done, false, __ATOMIC_RELEASE);
  bool started = xTaskCreate(ex_step_worker, "vm_prime_step", 4096, NULL, 5, NULL) == pdPASS;
  ck("prime step worker started", started);
  if (!started) { vm_exec_stop(); return; }

  // Step 0: Block 0 computes end = 6.0f -> holds before FOR (block 1)
  bool held = ex_wait_block(PR_BLK_FOR);
  ck("step: end=6.0f computed, holds before FOR", held && near_f(ex_f(PR_O_END), 6.0f));

  // Step 1: Dispatch FOR -> enters first iteration (d=2.0f) and holds before Block 2 inside span!
  ck("step into FOR loop", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PR_BLK_MOD);
  ck("step: FOR entered iteration d=2.0f, holds before MOD", held && near_f(ex_f(PR_O_D), 2.0f));

  // Step 2: Dispatch MOD -> computes 7 % 2 != 0 -> holds before ADD (block 3)
  ck("step MOD block", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PR_BLK_ADD);
  ck("step: MOD computed is_div=0, holds before ADD", held && near_f(ex_f(PR_O_IS_DIV), 0.0f));

  // Step 3: Dispatch ADD -> updates div_cnt=0 -> advances to next loop iteration (d=3.0f), holds before MOD
  ck("step ADD block", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PR_BLK_MOD);
  ck("step: second iteration d=3.0f, holds before MOD", held && near_f(ex_f(PR_O_D), 3.0f));

  // Step through remaining iterations (d=3, 4, 5, 6)
  // Each iteration has 2 blocks (MOD and ADD)
  // d=3: ADD
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_ADD);
  // d=4: MOD, ADD
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_MOD);
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_ADD);
  // d=5: MOD, ADD
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_MOD);
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_ADD);
  // d=6: MOD, ADD
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_MOD);
  vm_exec_control(VM_EXEC_NEXT); ex_wait_block(PR_BLK_ADD);

  // Loop finishes: advances out of FOR to Block 4 (PR_BLK_CHK)
  ck("step out of loop", vm_exec_control(VM_EXEC_NEXT) == NULL);
  held = ex_wait_block(PR_BLK_CHK);
  ck("step: loop completed, holds before CHK", held);

  // Step 4: Dispatch CHK -> computes is_prime=1.0f -> scan finishes!
  ck("step CHK block", vm_exec_control(VM_EXEC_NEXT) == NULL);
  ck("step: prime scan completed", ex_wait_done());
  ck("step: is_prime evaluated to 1.0f for 7", near_f(ex_f(PR_O_IS_PRIME), 1.0f));

  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);
  vm_sub_reset();
  vm_loader_reset();
}

/* ==========================================================================
   Runtime variable update (between scans) verification
   ========================================================================== */

#define OV_O_X 0      // mutable input variable (float)
#define OV_O_Y 1      // block output (float, mutable, usr_protected)
#define OV_O_K 2      // immutable constant (float = 10.0f, mutable = 0)
#define OV_O_ENO 3    // ENO object (bool, mutable, usr_protected)

#define OV_ACC_X 0
#define OV_ACC_K 1

#define OV_BLK_MUL 0

static const uint8_t c_mul_xk[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_MUL};

static bool err_has_tag(err_h err, err_tag_e tag) {
  for (err_h c = err; c != NULL; c = c->next_cause) {
    if (c->tag == tag) return true;
  }
  return false;
}

void test_runtime_override(void) {
  ESP_LOGI(TAG, "-- Runtime variable update between scans --");

  vm_loader_reset();
  vm_sub_reset();
  (void)vm_sub_init();
  vm_sub_set_sender(pipe_mock_sender);
  s_pipe_sub_calls = 0;
  s_pipe_sub_len = 0;

  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 8, [VM_REG_ACC] = 4, [VM_REG_BLK] = 2};
  ck("override arena opens", vm_store_open(DIRECT_POOL, counts) == NULL);

  // 1. Objects:
  // OV_O_X: mutable float variable (usr_protected = 0)
  vm_obj_head_t hx = hd(VM_OBJ_F, 1);
  hx.f.mutable = 1;
  hx.f.upd_resetable = 1;
  vm_obj_h ox = NULL;
  bool built = (vm_obj_create(&ox, OV_O_X, &hx, "x") == NULL);
  if (ox) *(float*)ox->payload = 0.0f;

  // OV_O_Y: block output, mutable = 1, usr_protected = 1
  vm_obj_head_t hy = hd(VM_OBJ_F, 1);
  hy.f.mutable = 1;
  hy.f.upd_resetable = 1;
  hy.f.usr_protected = 1;
  vm_obj_h oy = NULL;
  built = built && (vm_obj_create(&oy, OV_O_Y, &hy, "y") == NULL);
  if (oy) *(float*)oy->payload = 0.0f;

  // OV_O_K: immutable constant (mutable = 0)
  vm_obj_head_t hk = hd(VM_OBJ_F, 1);
  hk.f.mutable = 0;
  hk.f.upd_resetable = 0;
  vm_obj_h ok = NULL;
  built = built && (vm_obj_create(&ok, OV_O_K, &hk, "k") == NULL);
  if (ok) *(float*)ok->payload = 10.0f;

  // OV_O_ENO: block ENO, mutable = 1, usr_protected = 1
  vm_obj_head_t he = hd(VM_OBJ_B, 1);
  he.f.mutable = 1;
  he.f.upd_resetable = 1;
  he.f.usr_protected = 1;
  vm_obj_h oe = NULL;
  built = built && (vm_obj_create(&oe, OV_O_ENO, &he, "eno") == NULL);

  ck("override objects built", built);
  if (!built) return;

  // 2. Accessors:
  bool accs = true;
  accs = accs && (ex_acc(OV_ACC_X, OV_O_X) != NULL);
  accs = accs && (ex_acc(OV_ACC_K, OV_O_K) != NULL);
  ck("override accessors built", accs);
  if (!accs) return;

  // 3. Block 0: Expression computing y = x * k (x * 10.0f)
  vm_block_h b = NULL;
  err_h eb = vm_block_create(&b, OV_BLK_MUL,
                             &(vm_block_cfg_t){.block_idx = OV_BLK_MUL,
                                               .block_type = VM_BLK_EXPR,
                                               .in_cnt = 2,
                                               .q_cnt = 1,
                                               .en_cnt = 0,
                                               .on_error = VM_BLK_ERR_STOP,
                                               .custom_len = (uint16_t)vm_expr_size(0, sizeof(c_mul_xk)),
                                               .in_acc_ids = (const uint16_t[]){OV_ACC_X, OV_ACC_K},
                                               .out_obj_ids = (const uint16_t[]){OV_O_Y},
                                               .eno_obj_id = OV_O_ENO});
  ck("override block created", eb == NULL && b != NULL);
  if (eb || !b) return;

  vm_expr_code_t* code = (vm_expr_code_t*)vm_block_get_custom_data(b);
  code->const_cnt = 0;
  code->code_len = sizeof(c_mul_xk);
  memcpy(code->consts, c_mul_xk, sizeof(c_mul_xk));

  // 4. Subscribe to OV_O_Y (Object 1) for telemetry
  uint8_t sub_pkt[] = {0x04, 0x47, 0x01, (uint8_t)(OV_O_Y & 0xFF), (uint8_t)(OV_O_Y >> 8)};
  ck("subscribe to y telemetry", sys_interface_decode(sub_pkt, sizeof(sub_pkt)) == NULL);

  // 5. Start VM running
  vm_exec_set_mode(VM_RUN_RUNNING);
  ck("VM running mode active", vm_exec_mode() == VM_RUN_RUNNING);

  // 6. Baseline execution pass (x = 0.0f, k = 10.0f -> y = 0.0f)
  vm_exec_pass();
  ck("baseline pass y == 0.0f", near_f(ex_f(OV_O_Y), 0.0f));
  s_pipe_sub_calls = 0;

  // 7. Security / Protection Tests via incoming 0x43 packet:
  // 7a: Attempt write to user-protected block output y (OV_O_Y): MUST REJECT with ERR_VM_OBJ_USR_PROTECTED
  float try_val = 999.0f;
  uint8_t f_bad_prot[13] = {
      0x04, 0x43, 0x01,
      (uint8_t)(OV_O_Y & 0xFF), (uint8_t)(OV_O_Y >> 8),
      0x00, 0x00,
      0x04, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };
  memcpy(&f_bad_prot[9], &try_val, sizeof(try_val));
  err_h err_prot = sys_interface_decode(f_bad_prot, sizeof(f_bad_prot));
  ck("injection targeting usr_protected object rejected", err_has_tag(err_prot, ERR_VM_OBJ_USR_PROTECTED));
  ck("protected object y payload unchanged", near_f(ex_f(OV_O_Y), 0.0f));

  // 7b: Attempt write to immutable constant k (OV_O_K): MUST REJECT with ERR_VM_OBJ_NOT_MUTABLE
  uint8_t f_bad_mut[13] = {
      0x04, 0x43, 0x01,
      (uint8_t)(OV_O_K & 0xFF), (uint8_t)(OV_O_K >> 8),
      0x00, 0x00,
      0x04, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };
  memcpy(&f_bad_mut[9], &try_val, sizeof(try_val));
  err_h err_mut = sys_interface_decode(f_bad_mut, sizeof(f_bad_mut));
  ck("injection targeting immutable object rejected", err_has_tag(err_mut, ERR_VM_OBJ_NOT_MUTABLE));
  ck("immutable object k payload unchanged", near_f(ex_f(OV_O_K), 10.0f));

  // 7c: Attempt write to unknown object ID 999: MUST REJECT with ERR_VM_ACCESSOR_UNKNOWN_ID
  uint8_t f_bad_id[13] = {
      0x04, 0x43, 0x01,
      0xE7, 0x03,
      0x00, 0x00,
      0x04, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };
  memcpy(&f_bad_id[9], &try_val, sizeof(try_val));
  err_h err_id = sys_interface_decode(f_bad_id, sizeof(f_bad_id));
  ck("injection targeting unknown object id rejected", err_has_tag(err_id, ERR_VM_ACCESSOR_UNKNOWN_ID));

  // 8. Runtime Variable Update Between Scans:
  // Step A: Send packet updating x = 5.0f
  float new_x = 5.0f;
  uint8_t f_good[13] = {
      0x04, 0x43, 0x01,
      (uint8_t)(OV_O_X & 0xFF), (uint8_t)(OV_O_X >> 8),
      0x00, 0x00,
      0x04, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };
  memcpy(&f_good[9], &new_x, sizeof(new_x));
  err_h err_good = sys_interface_decode(f_good, sizeof(f_good));
  ck("valid runtime 0x43 packet accepted by decoder", err_good == NULL);
  ck("mid-scan isolation: x NOT yet modified before pass", near_f(ex_f(OV_O_X), 0.0f));

  // Step B: Run scan pass
  vm_exec_pass();
  ck("x updated to 5.0f at pass boundary", near_f(ex_f(OV_O_X), 5.0f));
  ck("block 0 triggered and computed y = 50.0f", near_f(ex_f(OV_O_Y), 50.0f));
  ck("telemetry emitted for updated y", s_pipe_sub_calls == 1);

  // Step C: Second pass with NO new packet
  vm_exec_pass();
  ck("quiescent pass: y stays 50.0f", near_f(ex_f(OV_O_Y), 50.0f));
  ck("quiescent pass: no extra telemetry emitted", s_pipe_sub_calls == 1);

  // Step D: Second runtime update with x = 12.0f
  float new_x2 = 12.0f;
  memcpy(&f_good[9], &new_x2, sizeof(new_x2));
  ck("second 0x43 packet accepted", sys_interface_decode(f_good, sizeof(f_good)) == NULL);
  ck("mid-scan isolation: x remains 5.0f before pass", near_f(ex_f(OV_O_X), 5.0f));

  vm_exec_pass();
  ck("x updated to 12.0f at pass boundary", near_f(ex_f(OV_O_X), 12.0f));
  ck("block 0 computed y = 120.0f", near_f(ex_f(OV_O_Y), 120.0f));
  ck("telemetry emitted for new y", s_pipe_sub_calls == 2);

  // Cleanup
  vm_exec_stop();
  vm_exec_set_sample_hook(NULL);
  vm_sub_reset();
  vm_loader_reset();
}



