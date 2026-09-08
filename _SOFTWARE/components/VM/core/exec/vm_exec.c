#include "vm_exec.h"
#include "esp_compiler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils.h"
#include "vm_block.h"
#include "vm_event.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

#define OWNER OWNER_VM_EXEC

static const char* TAG = "vm_exec";

/* ==========================================================================
   The palette

   `g_vm_blocks` / `g_vm_blocks_cnt` are defined in blocks/vm_blocks_table.c,
   not here. The supervisor dispatches through the declaration in vm_exec.h and
   must not know what is in the palette -- so the dependency runs blocks ->
   exec and never back, which is what keeps core/ free of every driver a real
   block will eventually pull in.
   ========================================================================== */

err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type) {
  if (!vm_block_fn_for(block_type)) {
    SE_RET_ERR(ERR_VM_BLK_UNKNOWN_TYPE, .blk_id = blk_id, .block_type = block_type);
  }
  return NULL;
}

/* Declared in vm_exec.h; the supervisor is the only writer. Latched once per
   pass so every block in one pass agrees about what time it is. */
uint64_t g_vm_pass_ms;

/* The task runs on core 1 so a tight pass cannot starve BLE on core 0.
   Priority sits above the idle task and below the drivers and the BLE host --
   a late pass is a late pass, a late radio is a dropped connection. */
#define VM_EXEC_TASK_PRIO 5
#define VM_EXEC_TASK_CORE 1

R_TASK_DEFINE(vm_exec_task_h, 3072);

static volatile vm_run_mode_e s_mode = VM_RUN_STOPPED;
static uint32_t s_pass_cnt;
static uint32_t s_last_pass_us;
static uint8_t s_span_depth;
static portMUX_TYPE s_program_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_program_locked;
static bool s_pass_active;

/* Control-task boundary. A complete pass must release every local handle before
   its program can be freed. Setting STOPPED also releases a frozen pass. */
vm_run_mode_e vm_exec_program_lock(void) {
  vm_run_mode_e previous;
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    if (!s_program_locked) {
      s_program_locked = true;
      previous = s_mode;
      s_mode = VM_RUN_STOPPED;
      portEXIT_CRITICAL(&s_program_mux);
      break;
    }
    portEXIT_CRITICAL(&s_program_mux);
    vTaskDelay(1);
  }
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    bool active = s_pass_active;
    portEXIT_CRITICAL(&s_program_mux);
    if (!active) return previous;
    vTaskDelay(1);
  }
}

void vm_exec_program_unlock(vm_run_mode_e mode) {
  portENTER_CRITICAL(&s_program_mux);
  s_mode = mode;
  s_program_locked = false;
  portEXIT_CRITICAL(&s_program_mux);
}

/* ==========================================================================
   Block watchdog

   One global watchdog, not per-block timing. Timing every block would cost a
   clock read per block per pass for something that almost never fires. Instead
   the supervisor publishes *which block it is currently executing* and a timer
   samples it: same block still current two samples running means it hung.
   Cost is one store per block and no clock reads at all -- and unlike the task
   watchdog it names the block that hung instead of just resetting.

   The word packs a sequence number alongside the block id, because the sampler
   has to distinguish "same block still running" from "same block again next
   pass". The id alone cannot: a one-block program would look permanently hung.

   Zero means "not inside a block", so the gaps between blocks and between
   passes read as idle rather than as a hang.

   [[VM_EXEC.MD]] specifies a timer *ISR* here, with the sampler setting a flag
   and something at task level building the trace, because error construction
   allocates and that is not ISR-safe. This uses an ordinary esp_timer instead,
   whose callback already runs at task level on the esp_timer task -- on core 0,
   so it keeps sampling while core 1 is stuck. That removes the need for the
   flag hand-off entirely (the error is built where it is detected) and avoids
   depending on CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD. The deferred
   hand-off would in fact have been worse here: a genuinely hung block never
   reaches the freeze point that would have drained the flag, so the report
   would never arrive.
   ========================================================================== */
static volatile uint32_t s_wd_word;  // (seq << 16) | block index; 0 = idle
static uint16_t s_wd_seq;
static esp_timer_handle_t s_wd_timer;
static uint32_t s_wd_last;

static __always_inline void wd_enter(uint16_t blk_id) {
  s_wd_word = ((uint32_t)(++s_wd_seq) << 16) | blk_id;
}

static __always_inline void wd_leave(void) {
  s_wd_word = 0;
}

static void wd_sample(void* arg) {
  (void)arg;
  uint32_t cur = s_wd_word;
  if (cur != 0 && cur == s_wd_last) {
    SE_EMIT_ERR(ERR_VM_EXEC_BLOCK_HUNG, .block_idx = (uint16_t)(cur & 0xFFFFu), .ms = VM_EXEC_BLOCK_WD_MS);
    /* Cleared so one hang reports once rather than every sample for as long as
       it lasts. If the block is still stuck at the next sample the word will
       not have changed, but s_wd_last no longer matches it, so the next report
       is one full period away. */
    s_wd_last = 0;
    return;
  }
  s_wd_last = cur;
}

/* ==========================================================================
   Telemetry sampling

   A hook rather than a call into a subscription module, because none exists
   yet -- live values are sampled for explicitly subscribed ids, and what does
   the subscribing is not the VM's to decide. What *is* the VM's to decide is
   where in the pass it happens, and that is not negotiable: subscriptions are
   sampled before `upd` is cleared, because clearing first would silently break
   "send on update", which is that flag's other customer.
   ========================================================================== */
static void (*s_sample_hook)(void);

void vm_exec_set_sample_hook(void (*hook)(void)) {
  s_sample_hook = hook;
}

/* ==========================================================================
   The pass
   ========================================================================== */

/**
 * `upd` is cleared by the supervisor, at the end of a pass -- not at the point
 * of consumption, and not at each section boundary.
 *
 * If a consumer cleared the flag when it ran, fan-out would break: two blocks
 * reading the same fresh object, and whichever ran first would steal the
 * update. Topological order already guarantees a writer runs before its
 * readers within the same pass, so every consumer sees the flag before it is
 * cleared here. Clearing per section would break that across sections -- a
 * value written in section 0 would no longer read as fresh in section 3, even
 * though section 3 runs after it in the same pass.
 *
 * The sweep respects `upd_resetable`. A non-resettable object is never cleared
 * by anyone; the flag is reserved for constants and user variables, where
 * freshness is not a meaningful signal. Such an object may still be wired to a
 * trigger pin -- its `upd` simply never clears, so the block reading it is
 * permanently active, which the editor shows on the block face rather than
 * rejecting.
 */
static void clear_upd(void) {
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  for (uint16_t i = 0; i < g->count; i++) {
    vm_obj_h o = (vm_obj_h)g->items[i];
    if (o && o->head.f.upd_resetable) o->head.f.upd = 0;
  }

  /* Dynamic objects are not in the registry -- they are heap-allocated behind
     a refcount and reached through a parent's pointer slot -- but a parsed
     message is exactly the kind of thing a downstream block waits on, so they
     have to be swept too or freshness would work for arena objects only. */
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    vm_obj_h o = vm_obj_dyn_get(i);
    if (o && o->head.f.upd_resetable) o->head.f.upd = 0;
  }
}

/**
 * A freeze point. The only place anything external may intervene, and the
 * reason a section is the unit it is: everything below lands between sections,
 * never inside one, so a section always traverses a consistent snapshot.
 *
 * Events are *not* here, and that is a deliberate step further than section
 * granularity: they are swapped once at the top of the pass, so every section
 * of one pass sees the same arrivals rather than each seeing whatever had
 * landed by the time it started. Only the overflow *report* is here, because
 * building an error allocates and the post path may be an ISR.
 */
static void freeze_point(void) {
  err_h e = vm_event_take_overflow();
  if (unlikely(e != NULL)) SE_push_to_handler(e);

  /* Freeze and resume take effect here too, which is what bounds freeze
     latency by the longest section rather than by a whole pass. The delay is a
     real block rather than a spin: the VM owns core 1 but does not own it
     exclusively, and a frozen VM should cost nothing. */
  while (s_mode == VM_RUN_FROZEN) {
    vTaskDelay(MSEC(10));
  }
}

/**
 * Every block in a running section is called. The supervisor decides nothing
 * about whether it should be: no activation test, no gating, no skipping.
 *
 * That is not a simplification, it is where the knowledge is. A block knows
 * what wakes it -- an arrival, a level, a deadline, nothing at all -- far
 * better than any table describing it from outside could, and it already holds
 * everything needed to answer: its pins, its enable list, its private state.
 * So it asks (vm_block_triggered(), vm_block_is_enabled()) and acts, and when
 * it decides not to act it says so with vm_block_set_ENO(b, false) and nothing
 * else -- its outputs stand, and the end-of-pass `upd` sweep below withdraws
 * their freshness for it.
 *
 * What is left here is the two things a block genuinely cannot do for itself:
 * honour cfg.on_error after it has already failed, and move the walk.
 */
static void run_block(vm_block_h b, vm_block_fn fn) {
  // per-call bits only; VM_BLK_RT_SPAN_BAD and anything sticky survives
  b->cfg.rt &= (uint8_t)~VM_BLK_RT_PER_CALL;

  /* g_vm_block_fault is one global, and a span owner is *mid-call* while the
     blocks inside its span run through here. Without saving it, the innermost
     block to finish decides two things it has no business deciding: it clears
     whatever fault the owner had already recorded, and it leaves its own behind
     for the owner's on_error to be applied to. So a FOR would have its ENO
     dropped because the last block in its body failed -- after that block's own
     on_error had already dealt with it -- and a FOR that genuinely failed would
     get away with it. Saving here is what keeps the flag per-block rather than
     per-nesting-level; the top-level walk saves and restores `false`. */
  const bool outer_fault = g_vm_block_fault;
  g_vm_block_fault = false;
  fn(b);

  /* cfg.on_error, finally enforced -- and it is literally what the name says:
     STOP publishes a false ENO, so everything downstream self-skips. CONTINUE
     leaves the flow below untouched, and the error is already on its way to
     the handler either way.

     This has to be out here rather than in the block, because by the time a
     body knows it failed it may already have published and returned. The
     outputs are *not* retracted: whatever the block wrote is what it computed,
     and the flow being stopped is what keeps anything from acting on it. */
  if (unlikely(g_vm_block_fault) && b->cfg.on_error == VM_BLK_ERR_STOP) {
    vm_block_set_ENO(b, false);
  }

  g_vm_block_fault = outer_fault;  // hand the owner back its own fault state
}

void vm_exec_run_range(uint16_t start, uint16_t end) {
  if (unlikely(s_span_depth >= VM_EXEC_MAX_SPAN_DEPTH)) {
    SE_EMIT_ERR(ERR_VM_EXEC_SPAN_DEPTH, .block_idx = start, .depth = VM_EXEC_MAX_SPAN_DEPTH);
    return;
  }
  s_span_depth++;

  for (uint16_t i = start; i < end;) {
    vm_block_h b = vm_block_by_id(i);
    if (unlikely(!b)) {
      i++;
      continue;
    }

    /* The load rejects a type the table does not fill
       (vm_exec_check_block_type), so a miss here means the table in force
       changed under a loaded program -- which only a test can do. Skipping is
       the only safe answer: there is nothing to call. */
    vm_block_fn fn = vm_block_fn_for(b->cfg.block_type);
    if (unlikely(!fn)) {
      i++;
      continue;
    }

    wd_enter(i);
    run_block(b, fn);
    wd_leave();

    uint16_t next = (uint16_t)(i + 1);

    /* Did the block just take over the range that follows it? If so the walk
       jumps, because otherwise every block in that span would run once from
       here *plus* however many times its owner ran it.

       Asked after the call, not before: only the block knows it is a span
       owner, and it says so by claiming (vm_block_claim_span()). A FOR claims
       before it decides anything else, so it still claims when it is disabled
       -- running the span zero times is not the same as letting this walk run
       it once. */
    if (unlikely(b->cfg.rt & VM_BLK_RT_SPAN)) {
      const vm_span_t* sp = vm_block_span(b);
      if (likely(sp && sp->start == next && sp->end > sp->start && sp->end <= end)) {
        next = sp->end;
      } else if (!(b->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
        /* A claim vm_block_claim_span() accepted but that does not fit where
           the walk actually is -- it does not start at the next block, or runs
           past the section. Sticky, so a standing condition reports once per
           program rather than once per pass. The walk still moves forward by
           one, so this degrades to "the span runs inline" rather than a hang. */
        b->cfg.rt |= VM_BLK_RT_SPAN_BAD;
        SE_EMIT_ERR(ERR_VM_EXEC_BAD_SPAN, .block_idx = b->cfg.block_idx, .start = sp ? sp->start : 0,
                    .end = sp ? sp->end : 0);
      }
    }

    i = next;
  }

  s_span_depth--;
}

void vm_exec_pass(void) {
  portENTER_CRITICAL(&s_program_mux);
  if (s_program_locked || s_pass_active ||
      (vm_exec_task_h && xTaskGetCurrentTaskHandle() == vm_exec_task_h && s_mode == VM_RUN_STOPPED)) {
    portEXIT_CRITICAL(&s_program_mux);
    return;
  }
  s_pass_active = true;
  portEXIT_CRITICAL(&s_program_mux);
  uint64_t t0 = vm_clock_us();
  g_vm_pass_ms = t0 / 1000u;

  /* Events, like the clock, are latched once for the whole pass rather than
     per section: what this drain pulls out of the queue is what every block
     sees, start to finish, and anything the last pass did not act on is
     overwritten by it. One instant per pass, for time and for arrivals alike. */
  vm_event_drain();

  uint16_t n = vm_section_count();
  uint16_t ran = 0;
  for (uint16_t s = 0; s < n; s++) {
    const vm_section_t* sec = vm_section_by_id(s);
    if (unlikely(!sec)) continue;  // id declared but never bound
    freeze_point();
    vm_exec_run_range(sec->start, sec->end);
    ran++;
  }

  /* No section actually bound -- either the program declared none, or it
     declared some and never uploaded them. Both mean the same thing to a pass:
     the whole order is one section, interruptible nowhere.

     The condition is "none bound", not "none declared", on purpose. A program
     whose section packets never arrived would otherwise load, count passes and
     execute nothing at all, with nothing to see -- the worst failure mode
     available here. Running the order it did load is at least observable, and
     matches what declaring no sections does. */
  if (ran == 0) {
    freeze_point();
    vm_exec_run_range(0, g_vm_store.reg[VM_REG_BLK].count);
  }

  // order within the pass: run sections -> sample subscriptions -> clear upd
  if (s_sample_hook) s_sample_hook();
  clear_upd();

  s_last_pass_us = (uint32_t)(vm_clock_us() - t0);
  s_pass_cnt++;
  portENTER_CRITICAL(&s_program_mux);
  s_pass_active = false;
  portEXIT_CRITICAL(&s_program_mux);
}

/* ==========================================================================
   The task
   ========================================================================== */

static void vm_exec_task(void* arg) {
  (void)arg;
  for (;;) {
    if (s_mode == VM_RUN_STOPPED) {
      vTaskDelay(MSEC(10));
      continue;
    }

    vm_exec_pass();

    portENTER_CRITICAL(&s_program_mux);
    if (!s_program_locked && s_mode == VM_RUN_STEP) s_mode = VM_RUN_FROZEN;
    portEXIT_CRITICAL(&s_program_mux);

    /* One tick per pass. taskYIELD() would not do: ESP-IDF watches the idle
       task of core 1 as well as core 0, and idle runs at priority 0, so
       yielding to an equal-priority peer never lets it run. At
       CONFIG_FREERTOS_HZ = 100 one tick is exactly the 10 ms floor already
       promised to the user, and it leaves core 1 usable by device drivers. */
    vTaskDelay(1);
  }
}

err_h vm_exec_start(void) {
  if (vm_exec_task_h != NULL) return NULL;  // idempotent

  if (!s_wd_timer) {
    const esp_timer_create_args_t args = {
        .callback = wd_sample,
        .name = "vm_blk_wd",
    };
    if (esp_timer_create(&args, &s_wd_timer) != ESP_OK) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
    (void)esp_timer_start_periodic(s_wd_timer, (uint64_t)VM_EXEC_BLOCK_WD_MS * 1000u);
  }

  R_TASK_START_ON_CORE(vm_exec_task_h, vm_exec_task, NULL, VM_EXEC_TASK_PRIO, VM_EXEC_TASK_CORE);
  if (vm_exec_task_h == NULL) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  ESP_LOGI(TAG, "supervisor started on core %d, %u block types in the table", VM_EXEC_TASK_CORE, g_vm_blocks_cnt);
  return NULL;
}

void vm_exec_stop(void) {
  vm_exec_set_mode(VM_RUN_STOPPED);
}

void vm_exec_set_mode(vm_run_mode_e mode) {
  portENTER_CRITICAL(&s_program_mux);
  if (!s_program_locked) s_mode = mode;
  portEXIT_CRITICAL(&s_program_mux);
}

vm_run_mode_e vm_exec_mode(void) {
  return s_mode;
}

uint32_t vm_exec_pass_count(void) {
  return s_pass_cnt;
}

uint32_t vm_exec_last_pass_us(void) {
  return s_last_pass_us;
}

void vm_exec_reset_stats(void) {
  s_pass_cnt = 0;
  s_last_pass_us = 0;
}

void vm_exec_reset(void) {
  vm_event_reset();
  s_span_depth = 0;
  s_wd_word = 0;
  s_wd_last = 0;
  g_vm_block_fault = false;
  g_vm_pass_ms = 0;
  vm_exec_reset_stats();
}
