#include "vm_bench.h"
#include "runit_board_cfg.h"

#if RUNIT_ENABLE_VM_BENCH

#include <esp_cpu.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vm_block.h"
#include "vm_block_expr.h"
#include "vm_blocks.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"

#define OWNER OWNER_VM_BASE

static const char* TAG = "vm_bench";

#define GRID_N 8
#define CELLS (GRID_N * GRID_N)

/* One pass is CELLS accesses. REPS keeps a pass long enough that the timer
   read at each end is noise; TRIALS lets the best pass win, so an interrupt
   landing mid-run shows up as a discarded trial rather than as the number. */
#define REPS 500
#define TRIALS 3

/* Accessor ids are handed out in creation order -- the bench does not care
   which id anything gets, only that each is unique and inside the registry. */
static uint16_t s_next_acc;

static vm_obj_h s_grid;          // VM_OBJ_PTR[8] -- the 2D container
static vm_obj_h s_rows[GRID_N];  // VM_OBJ_F[8]   -- one per row
static vm_obj_h s_flat;          // VM_OBJ_F[64]  -- same data, one object
static vm_obj_h s_rsel, s_csel;  // live row/column selectors for by-ref

static vm_accessor_t* s_flat_acc[CELLS];  // flat[i]
static vm_accessor_t* s_grid_acc[CELLS];  // grid[r][c], both literal
static vm_accessor_t* s_name_acc[CELLS];  // grid["rN"][c]
static vm_accessor_t* s_row_acc[GRID_N];  // grid[r], resolved to the row object
static vm_accessor_t* s_ref_acc;          // grid[rsel][csel], both by-ref
static uint8_t s_add_blk_buf[160];
static uint8_t s_blk_arr_buf[160];
static uint8_t s_blk_tag_buf[160];
static vm_block_h s_blk_scalar;
static vm_block_h s_blk_array;
static vm_block_h s_blk_tag;

/*
Expression-block fixtures.

Three programs of rising size over a *dedicated* input object, so the figures
are three points on one line and the per-pin and per-op costs fall out of the
differences between them:

  simple   2 pins,  1 op
  mid      4 pins,  3 ops
  hard     8 pins, 20 ops (13 binary, 7 unary) and 6 literals

Deliberately no transcendental in any of them. `sinf`/`logf`/`powf` are libm
calls whose cost has nothing to do with the VM, and one of them in the hard
program would swamp the twenty opcodes it is there to measure. What these
three time is the machine -- the dispatch, the stack, the pin cache, the
publish -- and a program using a transcendental pays libm on top of it.

Their own input object rather than s_flat: the block scenarios above write
s_flat[0] on every execution, so sharing it would leave these reading a value
that drifts with whatever ran first.
*/
#define EXPR_IN_CNT 8
#define EXPR_BUF 160
static uint8_t s_expr_simple_buf[EXPR_BUF] __attribute__((aligned(8)));
static uint8_t s_expr_mid_buf[EXPR_BUF] __attribute__((aligned(8)));
static uint8_t s_expr_hard_buf[EXPR_BUF] __attribute__((aligned(8)));
static vm_block_h s_expr_simple;
static vm_block_h s_expr_mid;
static vm_block_h s_expr_hard;
static vm_obj_h s_expr_in;   // VM_OBJ_F[8], holds 1.0 .. 8.0
static vm_obj_h s_expr_out;  // where all three publish
static vm_obj_h s_expr_eno;
static vm_accessor_t* s_expr_acc[EXPR_IN_CNT];


// keeps the optimiser from deleting a scenario whose result nobody reads
static volatile float s_sink;

static uint32_t s_cpu_mhz = 240;

#define OBJ_GRID 0
#define OBJ_ROW0 1  // rows occupy 1..8
#define OBJ_FLAT 9
#define OBJ_RSEL 10
#define OBJ_CSEL 11
#define OBJ_EXPR_IN 12
#define OBJ_EXPR_OUT 13
#define OBJ_EXPR_ENO 14

/* ==========================================================================
   Fixtures
   ========================================================================== */

static bool s_ok;
#define OKC(call)                 \
  do {                            \
    if (s_ok && (call) != NULL) { \
      s_ok = false;               \
    }                             \
  } while (0)

static err_h mk(vm_obj_h* out, uint16_t id, vm_obj_t_e type, uint16_t items, const char* name) {
  // items -> bytes is the caller's arithmetic; see vm_obj_build.h
  vm_obj_head_t h = {0};
  h.payload_size = (uint16_t)(items * vm_type_width(type));
  h.d.obj_t = (uint8_t)type;
  h.d.name_size = name ? (uint8_t)strlen(name) : 0;
  h.f.mutable = 1;
  return vm_obj_create(out, id, &h, name);
}

// create-and-bind with the next free accessor id
static err_h mk_acc(vm_accessor_t** out, uint16_t root, uint8_t idx_count) {
  return vm_accessor_create(out, s_next_acc++, root, idx_count);
}

/* Lays one expression block out in a static buffer exactly as the arena would:
   header, pin arrays, then custom_data carrying the code. cfg.in_cnt / q_cnt /
   custom_len have to be set before vm_block_custom_data(), which derives the
   offset from them. */
static vm_block_h expr_fixture(uint8_t* buf, uint16_t block_idx, uint8_t in_cnt, const uint32_t* ks, uint8_t k_cnt,
                               const uint8_t* code, uint16_t code_len) {
  memset(buf, 0, EXPR_BUF);
  vm_block_h b = (vm_block_h)buf;
  b->cfg.block_idx = block_idx;
  b->cfg.block_type = VM_BLK_EXPR;
  b->cfg.in_cnt = in_cnt;
  b->cfg.q_cnt = 1;
  b->cfg.custom_len = (uint16_t)vm_expr_size(k_cnt, code_len);
  b->cfg.eno = s_expr_eno;

  for (uint8_t i = 0; i < in_cnt; i++) vm_block_get_inputs(b)[i] = s_expr_acc[i];
  vm_block_get_outputs(b)[0] = s_expr_out;

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  c->const_cnt = k_cnt;
  c->code_len = code_len;
  for (uint8_t i = 0; i < k_cnt; i++) c->consts[i].u = ks[i];
  memcpy(&c->consts[k_cnt], code, code_len);
  return b;
}

static bool setup(void) {
  /* 3 accessors per cell, plus one per row, plus the two selectors and the
     by-ref chain -- sized here so a shape change fails loudly at open rather
     than silently past the end of the registry. */
  const uint16_t counts[VM_REG_CNT] = {
      [VM_REG_OBJ] = 16,
      [VM_REG_ACC] = CELLS * 3 + GRID_N + 3 + EXPR_IN_CNT,
      [VM_REG_BLK] = 0,
  };
  /* 16 kB, not 8: the registries now come out of this same pool, and an
     accessor carries its resolution cache, so 203 of them cost 20 B each
     rather than 8. The old figure left ~4 bytes. */
  s_next_acc = 0;
  s_ok = vm_store_open(16384, counts) == NULL;

  char rowname[GRID_N][3];
  for (int r = 0; r < GRID_N; r++) {
    rowname[r][0] = 'r';
    rowname[r][1] = (char)('0' + r);
    rowname[r][2] = '\0';
  }

  OKC(mk(&s_grid, OBJ_GRID, VM_OBJ_PTR, GRID_N, NULL));
  for (int r = 0; r < GRID_N; r++) {
    // tagged so the by-name scenario has something to match
    OKC(mk(&s_rows[r], (uint16_t)(OBJ_ROW0 + r), VM_OBJ_F, GRID_N, rowname[r]));
    OKC(vm_obj_link_direct(s_grid, (uint16_t)r, s_rows[r]));
  }
  OKC(mk(&s_flat, OBJ_FLAT, VM_OBJ_F, CELLS, NULL));
  OKC(mk(&s_rsel, OBJ_RSEL, VM_OBJ_U8, 1, NULL));
  OKC(mk(&s_csel, OBJ_CSEL, VM_OBJ_U8, 1, NULL));
  if (!s_ok) return false;

  // cell (r,c) holds r*8+c, so every full traversal sums to 2016
  for (int r = 0; r < GRID_N; r++) {
    float* row = (float*)s_rows[r]->payload;
    for (int c = 0; c < GRID_N; c++) row[c] = (float)(r * GRID_N + c);
  }
  for (int i = 0; i < CELLS; i++) ((float*)s_flat->payload)[i] = (float)i;

  for (int r = 0; r < GRID_N; r++) {
    for (int c = 0; c < GRID_N; c++) {
      int i = r * GRID_N + c;

      OKC(mk_acc(&s_grid_acc[i], OBJ_GRID, 2));
      OKC(vm_accessor_set_literal(s_grid_acc[i], 0, (uint32_t)r));
      OKC(vm_accessor_set_literal(s_grid_acc[i], 1, (uint32_t)c));

      OKC(mk_acc(&s_name_acc[i], OBJ_GRID, 2));
      OKC(vm_accessor_set_name(s_name_acc[i], 0, rowname[r], 2));
      OKC(vm_accessor_set_literal(s_name_acc[i], 1, (uint32_t)c));

      OKC(mk_acc(&s_flat_acc[i], OBJ_FLAT, 1));
      OKC(vm_accessor_set_literal(s_flat_acc[i], 0, (uint32_t)i));
    }
    OKC(mk_acc(&s_row_acc[r], OBJ_GRID, 1));
    OKC(vm_accessor_set_literal(s_row_acc[r], 0, (uint32_t)r));
  }

  /* The by-ref chain: two whole-object accessors feeding the index slots, so
     each access re-reads both selectors -- the sequencer / mux shape. */
  vm_accessor_t* rsel_acc = NULL;
  vm_accessor_t* csel_acc = NULL;
  OKC(mk_acc(&rsel_acc, OBJ_RSEL, 0));
  OKC(mk_acc(&csel_acc, OBJ_CSEL, 0));
  OKC(mk_acc(&s_ref_acc, OBJ_GRID, 2));
  OKC(vm_accessor_set_ref(s_ref_acc, 0, rsel_acc));
  OKC(vm_accessor_set_ref(s_ref_acc, 1, csel_acc));

  /* What the loader does at the end of every add_accessor. Done here for the
     same reason: the shapes that qualify are pre-resolved, the ones that do
     not (the 2-level and by-ref chains below) are left to walk -- so the table
     shows both paths as a program would actually run them. */
  for (int i = 0; i < CELLS; i++) {
    (void)vm_accessor_cache_build(s_flat_acc[i]);
    (void)vm_accessor_cache_build(s_grid_acc[i]);
    (void)vm_accessor_cache_build(s_name_acc[i]);
  }
  for (int r = 0; r < GRID_N; r++) (void)vm_accessor_cache_build(s_row_acc[r]);
  (void)vm_accessor_cache_build(rsel_acc);
  (void)vm_accessor_cache_build(csel_acc);
  (void)vm_accessor_cache_build(s_ref_acc);

  // 3 Block Mockup Fixtures:
  // 1. Scalar block: inputs are 0-index scalar accessors (flat_acc[0], flat_acc[1])
  memset(s_add_blk_buf, 0, sizeof(s_add_blk_buf));
  s_blk_scalar = (vm_block_h)s_add_blk_buf;
  s_blk_scalar->cfg.block_idx = 1;
  s_blk_scalar->cfg.block_type = 1;
  s_blk_scalar->cfg.in_cnt = 2;
  s_blk_scalar->cfg.q_cnt = 1;
  vm_block_get_inputs(s_blk_scalar)[0] = s_flat_acc[0];
  vm_block_get_inputs(s_blk_scalar)[1] = s_flat_acc[1];
  vm_block_get_outputs(s_blk_scalar)[0] = s_flat;

  // 2. 1D Array block: inputs index into array elements (s_grid_acc[2], s_grid_acc[3])
  memset(s_blk_arr_buf, 0, sizeof(s_blk_arr_buf));
  s_blk_array = (vm_block_h)s_blk_arr_buf;
  s_blk_array->cfg.block_idx = 2;
  s_blk_array->cfg.block_type = 1;
  s_blk_array->cfg.in_cnt = 2;
  s_blk_array->cfg.q_cnt = 1;
  vm_block_get_inputs(s_blk_array)[0] = s_grid_acc[2];
  vm_block_get_inputs(s_blk_array)[1] = s_grid_acc[3];
  vm_block_get_outputs(s_blk_array)[0] = s_flat;

  // 3. Tag accessor block: inputs use tag matching ("r0"[1], "r1"[1])
  memset(s_blk_tag_buf, 0, sizeof(s_blk_tag_buf));
  s_blk_tag = (vm_block_h)s_blk_tag_buf;
  s_blk_tag->cfg.block_idx = 3;
  s_blk_tag->cfg.block_type = 1;
  s_blk_tag->cfg.in_cnt = 2;
  s_blk_tag->cfg.q_cnt = 1;
  vm_block_get_inputs(s_blk_tag)[0] = s_name_acc[1];
  vm_block_get_inputs(s_blk_tag)[1] = s_name_acc[9];
  vm_block_get_outputs(s_blk_tag)[0] = s_flat;

  // 4. The expression blocks -- see the fixture note at the top of the file
  OKC(mk(&s_expr_in, OBJ_EXPR_IN, VM_OBJ_F, EXPR_IN_CNT, NULL));
  OKC(mk(&s_expr_out, OBJ_EXPR_OUT, VM_OBJ_F, 1, NULL));
  OKC(mk(&s_expr_eno, OBJ_EXPR_ENO, VM_OBJ_B, 1, NULL));
  if (!s_ok) return false;

  // pin n holds n+1, so no pin is zero and a divisor can be picked freely
  for (int i = 0; i < EXPR_IN_CNT; i++) ((float*)s_expr_in->payload)[i] = (float)(i + 1);

  /* Fresh, and staying fresh: these blocks are update-driven, so an input
     nothing has written reads as stale and every execution would measure the
     stand-down path instead of the expression. Nothing clears it here -- the
     end-of-pass sweep belongs to the supervisor, which the bench does not
     run. */
  s_expr_in->head.f.upd = 1;

  for (int i = 0; i < EXPR_IN_CNT; i++) {
    OKC(mk_acc(&s_expr_acc[i], OBJ_EXPR_IN, 1));
    OKC(vm_accessor_set_literal(s_expr_acc[i], 0, (uint32_t)i));
    (void)vm_accessor_cache_build(s_expr_acc[i]);
  }
  if (!s_ok) return false;

  /* IN0 + IN1 */
  static const uint8_t code_simple[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_ADD};
  /* (IN0 + IN1) - (IN2 * IN3) */
  static const uint8_t code_mid[] = {VM_EXPR_IN, 0, VM_EXPR_IN, 1,   VM_EXPR_ADD, VM_EXPR_IN,
                                     2,          VM_EXPR_IN, 3, VM_EXPR_MUL, VM_EXPR_SUB};
  /* Twenty operations over all eight pins and six literals, never deeper than
     three on the stack. The tail (MAX/MIN against a literal, then SQUARE/ABS)
     clamps the running value, which is what makes the expected result exactly
     9.0 rather than something the check has to approximate -- every opcode
     still executes, since nothing here is folded. */
  static const uint8_t code_hard[] = {
      VM_EXPR_IN, 1, VM_EXPR_IN, 2, VM_EXPR_ADD, VM_EXPR_K, 0, VM_EXPR_MUL,
      VM_EXPR_IN, 3, VM_EXPR_IN, 4, VM_EXPR_MUL, VM_EXPR_ADD, VM_EXPR_ABS,
      VM_EXPR_IN, 5, VM_EXPR_K, 1, VM_EXPR_MUL, VM_EXPR_ADD,
      VM_EXPR_IN, 6, VM_EXPR_K, 2, VM_EXPR_DIV, VM_EXPR_SUB, VM_EXPR_ABS,
      VM_EXPR_IN, 7, VM_EXPR_K, 3, VM_EXPR_ADD, VM_EXPR_SQUARE,
      VM_EXPR_K, 4, VM_EXPR_DIV, VM_EXPR_ADD, VM_EXPR_NEG, VM_EXPR_ABS,
      VM_EXPR_IN, 0, VM_EXPR_K, 5, VM_EXPR_MAX, VM_EXPR_MIN,
      VM_EXPR_SQUARE, VM_EXPR_ABS};
  // literals travel as the u32 the wire carries, so the float bits go in by union
  uint32_t ks[6];
  const float kv[6] = {2.0f, 0.5f, 10.0f, 1.0f, 100.0f, 3.0f};
  for (int i = 0; i < 6; i++) {
    vm_expr_k_t k = {.f = kv[i]};
    ks[i] = k.u;
  }
  s_expr_simple = expr_fixture(s_expr_simple_buf, 11, 2, NULL, 0, code_simple, sizeof(code_simple));
  s_expr_mid = expr_fixture(s_expr_mid_buf, 12, 4, NULL, 0, code_mid, sizeof(code_mid));
  s_expr_hard = expr_fixture(s_expr_hard_buf, 13, EXPR_IN_CNT, ks, 6, code_hard, sizeof(code_hard));

  return s_ok;
}

/* ==========================================================================
   Scenarios -- each performs exactly CELLS accesses and returns their sum
   ========================================================================== */

// the floor: what walking the same bytes costs with no VM in the way
static float bench_raw(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    const float* row = (const float*)s_rows[r]->payload;
    for (int c = 0; c < GRID_N; c++) acc += row[c];
  }
  return acc;
}

// one object, one literal index -- the cheapest accessor there is
static float bench_flat_literal(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_flat_acc[i]) == NULL) acc += v;
  }
  return acc;
}

// the 2D shape: same read, one VM_OBJ_PTR dereference deeper
static float bench_grid_literal(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_grid_acc[i]) == NULL) acc += v;
  }
  return acc;
}

/* Both indices read live from other objects. The selectors are written
   directly rather than through VM_OBJ_SET_VAL so the figure stays a pure
   read cost. */
static float bench_grid_ref(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    *(uint8_t*)s_rsel->payload = (uint8_t)r;
    for (int c = 0; c < GRID_N; c++) {
      *(uint8_t*)s_csel->payload = (uint8_t)c;
      float v = 0;
      if (VM_OBJ_GET_VAL(v, s_ref_acc) == NULL) acc += v;
    }
  }
  return acc;
}

// the message shape: a tag scan replaces the first literal. Averaged over all
// eight rows, so it covers both a first-slot and a last-slot match.
static float bench_grid_name(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = 0;
    if (VM_OBJ_GET_VAL(v, s_name_acc[i]) == NULL) acc += v;
  }
  return acc;
}

/* The fold-block shape: resolve the row once, then step its payload. Same 64
   values, but eight resolves instead of sixty-four. */
static float bench_row_payload(void) {
  float acc = 0;
  for (int r = 0; r < GRID_N; r++) {
    vm_obj_h row = NULL;
    if (vm_obj_get_obj(&row, s_row_acc[r]) != NULL) continue;
    vm_payload_t p = vm_make_payload(row);
    for (uint16_t i = 0; i < p.count; i++) {
      float v = 0;
      VM_PAYLOAD_GET_VAL(v, vm_payload_get_at(p, i));
      acc += v;
    }
  }
  return acc;
}

// the write path over the same chain -- adds the mutable check and the upd flag
static float bench_grid_write(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    float v = (float)i;
    if (VM_OBJ_SET_VAL(v, s_grid_acc[i]) == NULL) acc += 1.0f;
  }
  return acc;
}

static void block_add_execute(vm_block_h block) {
  IF_BLOCK_ENABLED(block) {
    const vm_accessor_t *in0 = NULL, *in1 = NULL;
    vm_obj_h out0 = NULL;
    BLOCK_CALL(vm_block_get_in(&in0, block, 0), block);
    BLOCK_CALL(vm_block_get_in(&in1, block, 1), block);
    BLOCK_CALL(vm_block_get_out(&out0, block, 0), block);
    float a = 0, b = 0;
    BLOCK_CALL(VM_OBJ_GET_VAL(a, in0), block);
    BLOCK_CALL(VM_OBJ_GET_VAL(b, in1), block);
    float sum = a + b;
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(sum, out0, 0), block);
    vm_block_set_eno(block, true);
  } else {
    vm_block_set_eno(block, false);
  }
}

static float bench_block_scalar(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_scalar);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}

static float bench_block_array(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_array);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}

static float bench_block_tag(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    block_add_execute(s_blk_tag);
    acc += ((float*)s_flat->payload)[0];
  }
  return acc;
}

/* The expression blocks, through the palette function itself rather than
   through a copy of it -- so what is timed includes everything a real pass
   pays: the trigger scan, the custom_data check, the bytecode walk, the
   publish and the ENO write. CELLS executions per call, like every other
   block row, so the reported figure is cycles per *execution*. */
static float bench_expr_simple(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    vm_blk_expr(s_expr_simple);
    acc += *(float*)s_expr_out->payload;
  }
  return acc;
}

static float bench_expr_mid(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    vm_blk_expr(s_expr_mid);
    acc += *(float*)s_expr_out->payload;
  }
  return acc;
}

static float bench_expr_hard(void) {
  float acc = 0;
  for (int i = 0; i < CELLS; i++) {
    vm_blk_expr(s_expr_hard);
    acc += *(float*)s_expr_out->payload;
  }
  return acc;
}



/* ==========================================================================
   Harness
   ========================================================================== */

typedef float (*bench_fn_t)(void);

static uint32_t s_raw_cyc_x10;

static void measure_cpu_mhz(void) {
  int64_t t0 = esp_timer_get_time();
  uint32_t c0 = esp_cpu_get_cycle_count();
  while (esp_timer_get_time() - t0 < 20000) {
  }
  uint32_t c1 = esp_cpu_get_cycle_count();
  int64_t t1 = esp_timer_get_time();
  uint32_t us = (uint32_t)(t1 - t0);
  if (us) s_cpu_mhz = (c1 - c0) / us;
  if (!s_cpu_mhz) s_cpu_mhz = 240;
}

/*
Hand core 0 back for one tick, between measurements and never inside one.

Everything here runs synchronously on `main`, which is priority 1 and blocks
nowhere: no measurement waits on anything, and console logging does not yield
either. So from the moment boot init finishes, IDLE0 -- priority 0 -- never
runs again, and the task watchdog it feeds trips at
CONFIG_ESP_TASK_WDT_TIMEOUT_S with `main` named as the CPU 0 hog. The self test
and the benchmark together are well past that, so this is not a matter of one
scenario being slow.

One tick is enough because the watchdog only needs IDLE0 to run at all. It
costs nothing measurable: it lands outside the timed region, each scenario
already warms the icache with a discarded call, and the reported figure is the
best of TRIALS runs.
*/
static void yield_to_idle(void) {
  vTaskDelay(1);
}

static void run(const char* name, bench_fn_t fn, float expect) {
  yield_to_idle();
  float got = fn();  // warm the icache, and prove the scenario actually works
  bool correct = (got > expect - 0.5f) && (got < expect + 0.5f);

  uint32_t best = UINT32_MAX;
  for (int t = 0; t < TRIALS; t++) {
    float sum = 0;
    uint32_t c0 = esp_cpu_get_cycle_count();
    for (int r = 0; r < REPS; r++) sum += fn();
    uint32_t c1 = esp_cpu_get_cycle_count();
    s_sink += sum;
    uint32_t d = c1 - c0;
    if (d < best) best = d;
  }

  uint32_t accesses = (uint32_t)REPS * CELLS;
  uint32_t cyc_x10 = (uint32_t)(((uint64_t)best * 10u) / accesses);
  uint32_t ns_x10 = (uint32_t)(((uint64_t)best * 10000u) / ((uint64_t)accesses * s_cpu_mhz));
  if (!s_raw_cyc_x10) s_raw_cyc_x10 = cyc_x10 ? cyc_x10 : 1;
  uint32_t rel_x10 = (uint32_t)(((uint64_t)cyc_x10 * 10u) / s_raw_cyc_x10);

  ESP_LOGI(TAG, "  %-36s %5lu.%lu %8lu.%lu %7lu.%lux%s", name, (unsigned long)(cyc_x10 / 10), (unsigned long)(cyc_x10 % 10), (unsigned long)(ns_x10 / 10), (unsigned long)(ns_x10 % 10), (unsigned long)(rel_x10 / 10), (unsigned long)(rel_x10 % 10), correct ? "" : "   <-- WRONG RESULT");
}

/* ==========================================================================
   Register-window probe

   Xtensa rotates the register file by 8 on every call8. When it wraps, the
   CPU takes a window-overflow exception to spill registers to the stack, and
   a matching underflow on the way back -- so a call chain that happens to sit
   on that boundary pays for both on every single call.

   Windows repeat with period 8, so running the identical workload from eight
   different starting depths separates the two possibilities cleanly: a
   sawtooth means the resolve path is paying window exceptions and the fix is
   to flatten the call chain; a flat line means the cost is the code itself
   and the call structure is irrelevant.
   ========================================================================== */

static volatile float s_depth_guard;

static float __attribute__((noinline)) depth_trampoline(int n, bench_fn_t fn) {
  if (n <= 0) return fn();
  float v = depth_trampoline(n - 1, fn);
  /* The store has to happen *after* the call, or GCC turns this into a tail
     call -- a jump, which does not rotate the window and would make the whole
     probe measure nothing. */
  s_depth_guard = v;
  return v;
}

static void run_depth_sweep(const char* name, bench_fn_t fn) {
  ESP_LOGI(TAG, "  window probe -- %s, same work from 8 call depths:", name);
  for (int d = 0; d < 8; d++) {
    yield_to_idle();
    uint32_t best = UINT32_MAX;
    for (int t = 0; t < TRIALS; t++) {
      float sum = 0;
      uint32_t c0 = esp_cpu_get_cycle_count();
      for (int r = 0; r < REPS; r++) sum += depth_trampoline(d, fn);
      uint32_t c1 = esp_cpu_get_cycle_count();
      s_sink += sum;
      uint32_t dd = c1 - c0;
      if (dd < best) best = dd;
    }
    uint32_t cyc_x10 = (uint32_t)(((uint64_t)best * 10u) / ((uint32_t)REPS * CELLS));
    ESP_LOGI(TAG, "    +%d frames   %5lu.%lu cyc/acc", d, (unsigned long)(cyc_x10 / 10), (unsigned long)(cyc_x10 % 10));
  }
}

void vm_bench_run(void) {
  ESP_LOGW(TAG, "==== VM object access benchmark ====");

  if (!setup()) {
    ESP_LOGE(TAG, "fixture setup failed -- arena too small or an id clashed");
    return;
  }
  measure_cpu_mhz();

  ESP_LOGI(TAG, "%lu MHz CPU, %dx%d float grid (%d cells), %d reps x %d trials, best trial kept", (unsigned long)s_cpu_mhz, GRID_N, GRID_N, CELLS, REPS, TRIALS);
  ESP_LOGI(TAG, "arena %lu/%lu B", (unsigned long)vm_store_used(), (unsigned long)vm_store_capacity());
  ESP_LOGI(TAG, "  %-36s %7s %10s %9s", "scenario", "cyc/acc", "ns/acc", "vs raw");

  // raw first: every other row is reported as a multiple of it
  run("raw C pointer walk", bench_raw, 2016.0f);
  run("flat[i]            1 literal", bench_flat_literal, 2016.0f);
  run("grid[r][c]         2 literal", bench_grid_literal, 2016.0f);
  run("grid[rsel][csel]   2 by-ref", bench_grid_ref, 2016.0f);
  run("grid[\"rN\"][c]      name + literal", bench_grid_name, 2016.0f);
  run("row resolved once, payload walk", bench_row_payload, 2016.0f);
  run("grid[r][c] WRITE   2 literal", bench_grid_write, (float)CELLS);
  run("block_add (scalar in/out)", bench_block_scalar, 2080.0f);
  run("block_add (1D array arr[2]+arr[3])", bench_block_array, 320.0f);
  run("block_add (tag \"r0\"+\"r1\")", bench_block_tag, 640.0f);

  /* Per *execution*, not per access -- as for the block_add rows above, so the
     "vs raw" column is a scale marker rather than a like-for-like ratio. Three
     sizes on one machine: the difference between them is what an added pin and
     an added opcode actually cost. */
  run("EXPR  2 pins,  1 op", bench_expr_simple, 192.0f);
  run("EXPR  4 pins,  3 ops", bench_expr_mid, -576.0f);
  run("EXPR  8 pins, 20 ops + 6 literals", bench_expr_hard, 576.0f);





  /* Probe the cheapest chained scenario: it has the fewest moving parts, so
     anything periodic in it is the call structure and not the work. */
  run_depth_sweep("flat[i] 1 literal", bench_flat_literal);

  ESP_LOGW(TAG, "==== benchmark done ====");
}

#else

void vm_bench_run(void) {
  // Benchmark disabled by RUNIT_ENABLE_VM_BENCH
}

#endif
