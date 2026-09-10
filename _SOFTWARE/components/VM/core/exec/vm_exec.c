#include "vm_exec.h"
#include "esp_compiler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils.h"
#include "vm_block.h"
#include "vm_event.h"
#include "vm_obj_dyn.h"
#include "vm_override.h"
#include "vm_store.h"

#define OWNER OWNER_VM_EXEC

static const char* TAG = "vm_exec";

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
static volatile uint32_t s_pass_cnt;
static volatile uint32_t s_last_pass_us;
static uint8_t s_span_depth;
static vm_block_h s_current_block;
static vm_span_t s_child_bounds;
static portMUX_TYPE s_program_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_program_locked;
static volatile bool s_pass_active;
static volatile bool s_cancel;
static bool s_waiting;
static uint16_t s_next_block = UINT16_MAX;
static vm_run_mode_e s_selected = VM_RUN_RUNNING;
static vm_run_mode_e s_resume = VM_RUN_STOPPED;

/* Cancel at a block boundary and wait for the stack to release all handles
   before the loader can replace the program. */
vm_run_mode_e vm_exec_program_lock(void) {
  vm_run_mode_e previous;
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    if (!s_program_locked) {
      s_program_locked = true;
      previous = s_mode;
      s_mode = VM_RUN_STOPPED;
      s_cancel = true;
      portEXIT_CRITICAL(&s_program_mux);
      break;
    }
    portEXIT_CRITICAL(&s_program_mux);
    vTaskDelay(1);
  }
  while (s_pass_active) {
    vTaskDelay(1);
  }
  return previous;
}

void vm_exec_program_unlock(vm_run_mode_e mode) {
  portENTER_CRITICAL(&s_program_mux);
  s_mode = mode;
  s_cancel = false;
  s_program_locked = false;
  portEXIT_CRITICAL(&s_program_mux);
}

// Block watchdog: published (seq << 16) | block_id sampled periodically by esp_timer on core 0
static volatile uint32_t s_wd_word;  // (seq << 16) | block index; 0 = idle
static uint16_t s_wd_seq;
static esp_timer_handle_t s_wd_timer;
static uint32_t s_wd_last;
static bool s_wd_reported;  // sampler-owned

static __always_inline void wd_enter(uint16_t blk_id) {
  if (++s_wd_seq == 0) ++s_wd_seq;  // block 0 must never publish the idle sentinel
  s_wd_word = ((uint32_t)s_wd_seq << 16) | blk_id;
}

static __always_inline void wd_leave(void) {
  s_wd_word = 0;
}

static void wd_sample(void* arg) {
  (void)arg;
  uint32_t cur = s_wd_word;
  if (cur != s_wd_last) s_wd_reported = false;
  if (cur != 0 && cur == s_wd_last && !s_wd_reported) {
    s_wd_reported = true;
    SE_EMIT_ERR(ERR_VM_EXEC_BLOCK_HUNG, .block_idx = (uint16_t)(cur & 0xFFFFu), .ms = VM_EXEC_BLOCK_WD_MS);
  }
  s_wd_last = cur;
}

// Telemetry sampling hook (called before end-of-pass upd sweep)
static void (*s_sample_hook)(void);

void vm_exec_set_sample_hook(void (*hook)(void)) {
  s_sample_hook = hook;
}

/* ==========================================================================
   The pass
   ========================================================================== */

// End-of-pass upd sweep: clears upd flag on resetable registry and dynamic objects
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
    vm_obj_h o = vm_obj_dyn_get_by_id(i);
    if (o && o->head.f.upd_resetable) o->head.f.upd = 0;
  }
}

/* Overflow reporting stays outside block dispatch. */
static void report_event_overflow(void) {
  err_h e = vm_event_take_overflow();
  if (unlikely(e != NULL)) SE_push_to_handler(e);


}

bool vm_exec_cancelled(void) {
  return s_cancel;
}

/* Keep the C stack, including FOR iterators, while waiting. A parent watchdog
   must be idle here too: waiting for the operator is not a hung block. */
static bool block_gate(uint16_t id) {
  // Fast path: in normal continuous run or single-pass step, do not acquire spinlock
  if (likely((s_mode == VM_RUN_RUNNING || s_mode == VM_RUN_STEP) && !s_cancel)) {
    return true;
  }

  wd_leave();
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    bool hold = s_mode == VM_RUN_FROZEN || s_mode == VM_RUN_SCAN || s_mode == VM_RUN_BLOCK;
    if (s_cancel || !hold) {
      bool run = !s_cancel;
      if (run && s_mode == VM_RUN_BLOCK_STEP) s_mode = VM_RUN_BLOCK;
      s_waiting = false;
      s_next_block = UINT16_MAX;
      portEXIT_CRITICAL(&s_program_mux);
      return run;
    }
    s_waiting = true;
    s_next_block = id;
    portEXIT_CRITICAL(&s_program_mux);
    vTaskDelay(1);
  }
}

// Dispatch block function and enforce cfg.on_error
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
    vm_block_set_eno(b, false);
  }

  g_vm_block_fault = outer_fault;  // hand the owner back its own fault state
}

void vm_exec_run_range(uint16_t start, uint16_t end) {
  // Reject self-recursion and range escapes before any nested block can act
  if (s_current_block &&
      (start != s_child_bounds.start || end <= start || end > s_child_bounds.end)) {
    g_vm_block_fault = true;
    if (!(s_current_block->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
      s_current_block->cfg.rt |= VM_BLK_RT_SPAN_BAD;
      SE_EMIT_ERR(ERR_VM_EXEC_BAD_SPAN, .block_idx = s_current_block->cfg.block_idx, .start = start, .end = end);
    }
    return;
  }
  if (unlikely(s_span_depth >= VM_EXEC_MAX_SPAN_DEPTH)) {
    g_vm_block_fault = true;
    SE_EMIT_ERR(ERR_VM_EXEC_SPAN_DEPTH, .block_idx = start, .depth = VM_EXEC_MAX_SPAN_DEPTH);
    return;
  }
  s_span_depth++;
  vm_block_h outer_block = s_current_block;
  const vm_span_t outer_bounds = s_child_bounds;
  const uint32_t outer_wd = s_wd_word;

  for (uint16_t i = start; i < end;) {
    if (vm_exec_cancelled()) break;
    vm_block_h b = vm_block_get_by_id(i);
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

    if (!block_gate(i)) break;
    s_current_block = b;
    s_child_bounds = (vm_span_t){(uint16_t)(i + 1), end};
    wd_enter(i);
    run_block(b, fn);
    wd_leave();

    uint16_t next = (uint16_t)(i + 1);

    // Span owner claimed the following range; advance walk past the span
    if (unlikely(b->cfg.rt & VM_BLK_RT_SPAN)) {
      const vm_span_t* sp = vm_block_get_span(b);
      if (likely(sp && sp->start == next && sp->end > sp->start && sp->end <= end)) {
        next = sp->end;
      } else if (!(b->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
        b->cfg.rt |= VM_BLK_RT_SPAN_BAD;
        SE_EMIT_ERR(ERR_VM_EXEC_BAD_SPAN, .block_idx = b->cfg.block_idx, .start = sp ? sp->start : 0,
                    .end = sp ? sp->end : 0);
      }
    }

    i = next;
  }

  s_span_depth--;
  s_current_block = outer_block;
  s_child_bounds = outer_bounds;
  /* Returning from a body is progress: resume timing the owner with a fresh
     token, rather than making repeated visits look like one stalled call. */
  if (outer_wd && !vm_exec_cancelled()) wd_enter((uint16_t)outer_wd);
}

void vm_exec_pass(void) {
  portENTER_CRITICAL(&s_program_mux);
  if (s_program_locked || s_pass_active || s_mode == VM_RUN_FROZEN ||
      s_mode == VM_RUN_SCAN || s_mode == VM_RUN_BLOCK ||
      (vm_exec_task_h && xTaskGetCurrentTaskHandle() == vm_exec_task_h && s_mode == VM_RUN_STOPPED)) {
    portEXIT_CRITICAL(&s_program_mux);
    return;
  }
  s_pass_active = true;
  portEXIT_CRITICAL(&s_program_mux);
  uint64_t t0 = vm_clock_us();
  g_vm_pass_ms = t0 / 1000u;

  /* Events, like the clock, are latched once for the whole pass: what this
     drain pulls out of the queue is what every block sees, start to finish. */
  vm_event_drain();
  vm_override_drain();

  report_event_overflow();
  vm_exec_run_range(0, g_vm_store.reg[VM_REG_BLK].count);

  // Order within pass: execute blocks -> sample subscriptions -> clear upd
  bool completed = !vm_exec_cancelled();
  if (completed && s_sample_hook) s_sample_hook();
  clear_upd();

  portENTER_CRITICAL(&s_program_mux);
  if (completed) {
    s_last_pass_us = (uint32_t)(vm_clock_us() - t0);
    s_pass_cnt++;
  }
  if (!s_program_locked && s_mode == VM_RUN_STEP) {
    s_mode = VM_RUN_FROZEN;
    s_resume = VM_RUN_SCAN;
  }
  if (completed && s_mode == VM_RUN_FROZEN && s_resume == VM_RUN_STEP) s_resume = VM_RUN_SCAN;
  if (completed && s_mode == VM_RUN_FROZEN && s_resume == VM_RUN_BLOCK_STEP) s_resume = VM_RUN_BLOCK;
  // Empty programs also consume one NEXT. No dispatch means no gate did it.
  if (s_mode == VM_RUN_BLOCK_STEP) s_mode = VM_RUN_BLOCK;
  s_pass_active = false;
  portEXIT_CRITICAL(&s_program_mux);
}

/* ==========================================================================
   The task
   ========================================================================== */

static void vm_exec_task(void* arg) {
  (void)arg;
  for (;;) {
    vm_run_mode_e mode = vm_exec_mode();
    if (mode == VM_RUN_STOPPED || mode == VM_RUN_FROZEN || mode == VM_RUN_SCAN || mode == VM_RUN_BLOCK) {
      vTaskDelay(MSEC(10));
      continue;
    }

    vm_exec_pass();

    // Yield 1 tick per pass to ensure core-1 idle task and drivers run
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

  DBG(ESP_LOGI(TAG, "supervisor started on core %d, %u block types in the table", VM_EXEC_TASK_CORE, g_vm_blocks_cnt););
  return NULL;
}

void vm_exec_stop(void) {
  vm_exec_set_mode(VM_RUN_STOPPED);
}

void vm_exec_set_mode(vm_run_mode_e mode) {
  portENTER_CRITICAL(&s_program_mux);
  if (!s_program_locked && mode <= VM_RUN_BLOCK_STEP) {
    if (mode == VM_RUN_FROZEN && s_mode != VM_RUN_FROZEN) s_resume = s_mode;
    if (mode == VM_RUN_RUNNING) s_selected = VM_RUN_RUNNING;
    if (mode == VM_RUN_STEP || mode == VM_RUN_SCAN) s_selected = VM_RUN_SCAN;
    if (mode == VM_RUN_BLOCK || mode == VM_RUN_BLOCK_STEP) s_selected = VM_RUN_BLOCK;
    s_mode = mode;
  }
  portEXIT_CRITICAL(&s_program_mux);
}

vm_run_mode_e vm_exec_mode(void) {
  return s_mode;
}

vm_exec_status_t vm_exec_status(void) {
  portENTER_CRITICAL(&s_program_mux);
  vm_exec_status_t status = {s_mode, s_next_block, s_pass_active, s_waiting};
  portEXIT_CRITICAL(&s_program_mux);
  return status;
}

err_h vm_exec_control(vm_exec_command_e command) {
  if (command == VM_EXEC_RESET_TO_START) {
    (void)vm_exec_program_lock();
    clear_upd();
    s_resume = s_selected;
    vm_exec_program_unlock(s_selected == VM_RUN_RUNNING ? VM_RUN_FROZEN : s_selected);
    return NULL;
  }
  portENTER_CRITICAL(&s_program_mux);
  bool valid = !s_program_locked;
  if (valid) switch (command) {
    case VM_EXEC_SCAN_MODE:
      s_selected = s_mode = VM_RUN_SCAN;
      break;
    case VM_EXEC_BLOCK_MODE:
      s_selected = s_mode = VM_RUN_BLOCK;
      break;
    case VM_EXEC_NORMAL_MODE:
      s_selected = s_mode = VM_RUN_RUNNING;
      break;
    case VM_EXEC_ONCE:
      valid = s_selected == VM_RUN_SCAN && (s_mode == VM_RUN_SCAN || s_mode == VM_RUN_FROZEN);
      if (valid) s_mode = VM_RUN_STEP;
      break;
    case VM_EXEC_NEXT:
      valid = s_selected == VM_RUN_BLOCK && s_mode == VM_RUN_BLOCK && (!s_pass_active || s_waiting);
      if (valid) s_mode = VM_RUN_BLOCK_STEP;
      break;
    case VM_EXEC_PAUSE:
      if (s_mode != VM_RUN_FROZEN) { s_resume = s_mode; s_mode = VM_RUN_FROZEN; }
      break;
    case VM_EXEC_RESUME:
      if (s_mode == VM_RUN_FROZEN) s_mode = s_resume;
      break;
    default:
      valid = false;
      break;
  }
  uint8_t mode = (uint8_t)s_mode;
  portEXIT_CRITICAL(&s_program_mux);
  if (!valid) SE_RET_ERR(ERR_VM_EXEC_CONTROL, .command = (uint8_t)command, .mode = mode);
  return NULL;
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
  vm_override_reset();
  s_span_depth = 0;
  s_current_block = NULL;
  s_child_bounds = (vm_span_t){0, 0};
  portENTER_CRITICAL(&s_program_mux);
  s_waiting = false;
  s_next_block = UINT16_MAX;
  s_selected = VM_RUN_RUNNING;
  s_resume = VM_RUN_STOPPED;
  portEXIT_CRITICAL(&s_program_mux);
  wd_leave();  // sampler history stays owned by the timer task
  g_vm_block_fault = false;
  g_vm_pass_ms = 0;
  vm_exec_reset_stats();
}
