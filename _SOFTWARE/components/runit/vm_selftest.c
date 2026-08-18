#include "vm_selftest.h"
#include <esp_log.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_interface.h"
#include "vm_block.h"
#include "vm_block_branch.h"
#include "vm_block_expr.h"
#include "vm_block_for.h"
#include "vm_event.h"
#include "vm_exec.h"
#include "vm_blocks.h"
#include "vm_loader.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_obj_dyn.h"
#include "vm_section.h"

#define OWNER OWNER_VM_BASE

static const char* TAG = "vm_selftest";

static int s_pass;
static int s_fail;

static void ck(const char* what, bool ok) {
  if (ok) {
    s_pass++;
    ESP_LOGI(TAG, "  PASS  %s", what);
  } else {
    s_fail++;
    ESP_LOGE(TAG, "  FAIL  %s", what);
  }
}

/* The direct-API stages share the one store with the upload stages, so each
   opens it fresh rather than carrying a scratch arena of its own -- there is
   one allocator now, and a test that used a second one would not be testing
   the thing that ships. */
#define DIRECT_POOL 4096

static uint16_t s_acc_id;  // handed out in creation order by the direct stages

static void direct_arena_reset(void) {
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 32, [VM_REG_ACC] = 24, [VM_REG_BLK] = 8, [VM_REG_SEC] = 4};
  (void)vm_store_open(DIRECT_POOL, counts);
  s_acc_id = 0;
}

/* items -> bytes is the caller's arithmetic now, so the tests do it the same
   way a block compiler would. Everything else about the head is left zero. */
static vm_obj_head_t hd(vm_obj_t_e type, uint16_t items) {
  vm_obj_head_t h = {0};
  h.payload_size = (uint16_t)(items * vm_type_width(type));
  h.d.obj_t = (uint8_t)type;
  return h;
}

// create + bind in one step; returns NULL on failure so callers can assert
static vm_obj_h mk(uint16_t id, vm_obj_t_e type, uint16_t items, const char* name, bool mutable_) {
  vm_obj_head_t h = hd(type, items);
  h.d.name_size = name ? (uint8_t)strlen(name) : 0;
  h.f.mutable = mutable_ ? 1 : 0;
  vm_obj_h o = NULL;
  if (vm_obj_create(&o, id, &h, name) != NULL) {
    return NULL;
  }
  return o;
}

/* A float literal as the u32 the wire carries. The union is the format's, not
   a trick of the test: see vm_expr_k_t. */
static uint32_t kf(float f) {
  vm_expr_k_t k = {.f = f};
  return k.u;
}

static bool near_f(float a, float b) {
  return fabsf(a - b) < 1e-4f;
}

/* ==========================================================================
   A -- header helpers and the type tables
   ========================================================================== */

static void test_header_helpers(void) {
  ESP_LOGI(TAG, "-- A: header helpers / type tables --");
  direct_arena_reset();

  /* The shift table is load-bearing: vm_obj_items_cnt() divides by shifting,
     so a wrong entry silently returns the wrong element count everywhere.
     Check it against the size table for every real type. */
  const vm_obj_t_e types[] = {VM_OBJ_PTR, VM_OBJ_U8, VM_OBJ_U32, VM_OBJ_I32, VM_OBJ_F, VM_OBJ_B, VM_OBJ_STR, VM_OBJ_U64};
  bool widths_ok = true;
  bool counts_ok = true;
  for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    uint8_t w = vm_type_width(types[i]);
    if (w == 0 || (w & (w - 1)) != 0) widths_ok = false;  // must be a power of two
    vm_obj_h o = NULL;
    vm_obj_head_t h3 = hd(types[i], 3);
    if (vm_obj_create(&o, VM_ID_NONE, &h3, NULL) != NULL || !o) {
      counts_ok = false;
      continue;
    }
    if (vm_obj_items_cnt(o) != 3) counts_ok = false;
    if (vm_obj_payload_size(o) != (uint16_t)(3 * w)) counts_ok = false;
    if (vm_obj_type_size(o) != w) counts_ok = false;
  }
  ck("every type width is a non-zero power of two", widths_ok);
  ck("items_cnt/payload_size agree with width, all types", counts_ok);
  ck("vm_type_width rejects out-of-range type", vm_type_width((vm_obj_t_e)200) == 0);
  ck("vm_type_width(NONE) == 0", vm_type_width(VM_OBJ_NONE) == 0);

  /* VM_OBJ_SHIFT_PACK is what the hot path actually indexes with -- the two
     tables are only consulted by cold code from here on, so nothing but this
     check couples them. An array element is not an integer constant
     expression in C, so a _Static_assert cannot do it. */
  bool pack_ok = true;
  for (unsigned t = 0; t < sizeof(vm_obj_type_shifts) / sizeof(vm_obj_type_shifts[0]); t++) {
    if (vm_type_shift((uint8_t)t) != vm_obj_type_shifts[t]) pack_ok = false;
    if (vm_type_ok((uint8_t)t) && (1u << vm_type_shift((uint8_t)t)) != vm_obj_type_sizes[t]) pack_ok = false;
  }
  ck("packed shifts match both type tables", pack_ok);
  ck("vm_type_ok spans exactly PTR..U64", !vm_type_ok(VM_OBJ_NONE) && vm_type_ok(VM_OBJ_PTR) && vm_type_ok(VM_OBJ_U64) && !vm_type_ok(VM_OBJ_U64 + 1) && !vm_type_ok(15));

  /* The offset form must reject anything the count form did, including the
     wrap that a 32-bit by-ref index could otherwise produce. */
  direct_arena_reset();
  vm_obj_h eo = NULL;
  vm_obj_head_t h64 = hd(VM_OBJ_U64, 4);
  bool elem_ok = vm_obj_create(&eo, VM_ID_NONE, &h64, NULL) == NULL && eo;
  if (elem_ok) {
    elem_ok = vm_obj_elem_ptr(eo, 0) == eo->payload && vm_obj_elem_ptr(eo, 3) == eo->payload + 24 && vm_obj_elem_ptr(eo, 4) == NULL && vm_obj_elem_ptr(eo, 0x20000000u) == NULL &&  // would wrap to offset 0 if shifted unguarded
              vm_obj_elem_ptr(eo, UINT32_MAX) == NULL;
  }
  ck("elem_ptr bounds, including a shift that would wrap", elem_ok);

  direct_arena_reset();
  vm_obj_h named = mk(0, VM_OBJ_U32, 2, "abc", true);
  vm_obj_h anon = mk(1, VM_OBJ_U32, 2, NULL, true);
  ck("total_size includes name", named && vm_obj_total_size(named) == 4 + 8 + 3);
  ck("total_size without name", anon && vm_obj_total_size(anon) == 4 + 8);
  uint8_t nl = 0;
  ck("untagged object has no tag", anon && vm_obj_tag(anon, &nl) == NULL);
  ck("vm_obj_payload non-NULL when sized", named && vm_obj_payload(named) != NULL);

  vm_payload_t p = vm_obj_as_payload(named);
  ck("as_payload type/count", p.type == VM_OBJ_U32 && p.count == 2 && p.ptr == named->payload);
  vm_payload_t e1 = vm_payload_at(p, 1);
  ck("payload_at steps by width", e1.ptr == (uint8_t*)p.ptr + 4 && e1.count == 1);
  ck("payload_at out of range -> NULL", vm_payload_at(p, 2).ptr == NULL);
}

/* ==========================================================================
   B -- value conversion
   ========================================================================== */

static void test_conversion(void) {
  ESP_LOGI(TAG, "-- B: value conversion --");
  direct_arena_reset();

  mk(0, VM_OBJ_U8, 1, NULL, true);
  mk(1, VM_OBJ_I32, 1, NULL, true);
  mk(2, VM_OBJ_F, 1, NULL, true);
  mk(3, VM_OBJ_U64, 1, NULL, true);
  mk(4, VM_OBJ_U32, 1, NULL, true);

  static const vm_index_t i0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_u8 = {.id = 0, .count = 1, .indices = i0};
  static const vm_accessor_t a_i32 = {.id = 1, .count = 1, .indices = i0};
  static const vm_accessor_t a_f = {.id = 2, .count = 1, .indices = i0};
  static const vm_accessor_t a_u64 = {.id = 3, .count = 1, .indices = i0};
  static const vm_accessor_t a_u32 = {.id = 4, .count = 1, .indices = i0};

  // widening
  uint8_t in_u8 = 200;
  float out_f = 0;
  ck("U8 200 -> float 200", VM_OBJ_SET_VAL(in_u8, &a_u8) == NULL && VM_OBJ_GET_VAL(out_f, &a_u8) == NULL && out_f == 200.0f);

  int32_t in_i = -5;
  out_f = 0;
  ck("I32 -5 -> float -5", VM_OBJ_SET_VAL(in_i, &a_i32) == NULL && VM_OBJ_GET_VAL(out_f, &a_i32) == NULL && out_f == -5.0f);

  uint64_t in_big = 0x0123456789ABCDEFull;
  uint64_t out_big = 0;
  ck("U64 round-trip at 4-byte-aligned payload", VM_OBJ_SET_VAL(in_big, &a_u64) == NULL && VM_OBJ_GET_VAL(out_big, &a_u64) == NULL && out_big == 0x0123456789ABCDEFull);

  /* float -> integer rounds rather than truncating: a value block feeding
     179.6 into a servo angle should land on 180. */
  float in_f = 179.6f;
  int32_t out_i = 0;
  ck("float 179.6 -> int 180 (rounds, not truncates)", VM_OBJ_SET_VAL(in_f, &a_f) == NULL && VM_OBJ_GET_VAL(out_i, &a_f) == NULL && out_i == 180);

  in_f = -3.7f;
  out_i = 0;
  ck("float -3.7 -> int -4", VM_OBJ_SET_VAL(in_f, &a_f) == NULL && VM_OBJ_GET_VAL(out_i, &a_f) == NULL && out_i == -4);

  // vm_f_to_i guards: a plain cast of these to an integer would be UB
  ck("f_to_i(NaN) == 0", vm_f_to_i(0.0f / 0.0f) == 0);
  ck("f_to_i(+huge) saturates high", vm_f_to_i(1e30f) == INT64_MAX);
  ck("f_to_i(-huge) saturates low", vm_f_to_i(-1e30f) == INT64_MIN);
  ck("f_to_i rounds .5 away from zero", vm_f_to_i(2.5f) == 3 && vm_f_to_i(-2.5f) == -3);

  /* Documented and deliberate: the destination's own range is NOT clamped,
     so narrowing still wraps. Pinned here so adding destination clamping
     later has to be a conscious change rather than a silent one. */
  in_f = 300.0f;
  uint8_t out_u8 = 0;
  ck("float 300 -> uint8 wraps to 44 (no dest clamp)", VM_OBJ_SET_VAL(in_f, &a_f) == NULL && VM_OBJ_GET_VAL(out_u8, &a_f) == NULL && out_u8 == 44);

  /* VM_VAL_OF must write the union member VM_TYPE_OF's answer is read back
     through. A narrow source landing in .u32 while the tag says U8/B reads
     correctly on a little-endian target and wrong on any other, so pin every
     narrow source type here rather than trusting byte order. */
  bool in_b = true;
  uint32_t out_b = 0;
  ck("bool true -> U8 object reads 1", VM_OBJ_SET_VAL(in_b, &a_u8) == NULL && VM_OBJ_GET_VAL(out_b, &a_u8) == NULL && out_b == 1);

  char in_c = 'A';
  uint32_t out_c = 0;
  ck("char 'A' -> U8 object reads 65", VM_OBJ_SET_VAL(in_c, &a_u8) == NULL && VM_OBJ_GET_VAL(out_c, &a_u8) == NULL && out_c == 65);

  uint8_t in_u8b = 0xC3;
  float out_u8f = 0;
  ck("U8 0xC3 -> float 195", VM_OBJ_SET_VAL(in_u8b, &a_u8) == NULL && VM_OBJ_GET_VAL(out_u8f, &a_u8) == NULL && out_u8f == 195.0f);

  // storing wider than the object holds truncates on the way in
  uint32_t in_u32 = 0x12345678;
  uint8_t narrow = 0;
  ck("U32 stored into U8 object truncates", VM_OBJ_SET_VAL(in_u32, &a_u8) == NULL && VM_OBJ_GET_VAL(narrow, &a_u8) == NULL && narrow == 0x78);

  // reading a PTR/STR payload yields zero rather than garbage
  in_u32 = 7;
  (void)VM_OBJ_SET_VAL(in_u32, &a_u32);
  vm_payload_t praw = {.ptr = NULL, .count = 0, .type = VM_OBJ_STR, ._pad = 0};
  vm_val_t vraw = vm_payload_read(praw);
  ck("payload_read of NULL ptr is zero", vraw.u64 == 0);
}

/* ==========================================================================
   C -- accessor resolution
   ========================================================================== */

static void test_resolution(void) {
  ESP_LOGI(TAG, "-- C: accessor resolution --");
  direct_arena_reset();

  vm_obj_h arr = mk(0, VM_OBJ_U32, 4, NULL, true);
  vm_obj_h sel = mk(1, VM_OBJ_U8, 1, NULL, true);
  vm_obj_h parent = mk(2, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h leaf = mk(3, VM_OBJ_F, 1, "leaf", true);
  vm_obj_h ro = mk(4, VM_OBJ_F, 1, NULL, false);  // not mutable
  ck("fixtures built", arr && sel && parent && leaf && ro);

  uint32_t* cells = (uint32_t*)arr->payload;
  cells[0] = 10;
  cells[1] = 20;
  cells[2] = 30;
  cells[3] = 40;
  *(uint8_t*)sel->payload = 2;
  *(float*)leaf->payload = 1.25f;
  ck("link child into parent slot 1", vm_obj_link_direct(parent, 1, leaf) == NULL);

  // literal index
  static const vm_index_t lit2[] = {{.kind = VM_IDX_LITERAL, .value = 2}};
  static const vm_accessor_t a_lit = {.id = 0, .count = 1, .indices = lit2};
  uint32_t got = 0;
  ck("literal index reads arr[2] == 30", VM_OBJ_GET_VAL(got, &a_lit) == NULL && got == 30);

  /* by-ref index: the position is read live from another object, so the same
     accessor must follow `sel` when it changes. */
  static const vm_accessor_t a_sel = {.id = 1, .count = 0, .indices = NULL};
  static const vm_index_t byref[] = {{.kind = VM_IDX_REF, .ref = &a_sel}};
  static const vm_accessor_t a_dyn = {.id = 0, .count = 1, .indices = byref};
  got = 0;
  ck("by-ref index reads arr[sel=2] == 30", VM_OBJ_GET_VAL(got, &a_dyn) == NULL && got == 30);
  *(uint8_t*)sel->payload = 0;
  got = 0;
  ck("by-ref follows sel=0 -> 10 (live, not cached)", VM_OBJ_GET_VAL(got, &a_dyn) == NULL && got == 10);

  // name index through a PTR parent, then the value step
  static const vm_index_t named[] = {VM_IDX_BY_NAME("leaf"), {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_named = {.id = 2, .count = 2, .indices = named};
  float f = 0;
  ck("name index finds child at slot 1", VM_OBJ_GET_VAL(f, &a_named) == NULL && f == 1.25f);

  // failure modes
  static const vm_index_t oob[] = {{.kind = VM_IDX_LITERAL, .value = 9}};
  static const vm_accessor_t a_oob = {.id = 0, .count = 1, .indices = oob};
  ck("index past end -> OOB", VM_OBJ_GET_VAL(got, &a_oob) != NULL);

  static const vm_accessor_t a_badid = {.id = 999, .count = 0, .indices = NULL};
  ck("unknown root id -> UNKNOWN_ID", VM_OBJ_GET_VAL(got, &a_badid) != NULL);

  /* slot 0 of parent was never linked, so descending through it must report
     rather than dereference NULL */
  static const vm_index_t via_null[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_null = {.id = 2, .count = 2, .indices = via_null};
  ck("descend through unlinked slot -> NULL_OBJ", VM_OBJ_GET_VAL(f, &a_null) != NULL);

  // chaining past a non-PTR element
  static const vm_index_t deep[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_mismatch = {.id = 0, .count = 2, .indices = deep};
  ck("chain through non-PTR -> TYPE_MISMATCH", VM_OBJ_GET_VAL(got, &a_mismatch) != NULL);

  // name lookup on a scalar array has no tags to match
  static const vm_index_t nm[] = {VM_IDX_BY_NAME("leaf")};
  static const vm_accessor_t a_name_scalar = {.id = 0, .count = 1, .indices = nm};
  ck("name index on scalar array -> TYPE_MISMATCH", VM_OBJ_GET_VAL(got, &a_name_scalar) != NULL);

  /* Mutability gates writes only -- reading a read-only calibration table is
     legitimate, so the check must not fire on the read path. */
  static const vm_index_t ro_i0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_ro = {.id = 4, .count = 1, .indices = ro_i0};
  *(float*)ro->payload = 9.5f;
  float rov = 0.0f;
  ck("read from non-mutable object succeeds", VM_OBJ_GET_VAL(rov, &a_ro) == NULL && rov == 9.5f);
  float wv = 1.0f;
  ck("write to non-mutable object -> NOT_MUTABLE", VM_OBJ_SET_VAL(wv, &a_ro) != NULL);
  ck("rejected write left the value alone", *(float*)ro->payload == 9.5f);

  /* Whole-array iteration -- the shape a fold block (Sum, Average, Min/Max)
     uses: resolve once to a payload, then step it element by element. */
  static const vm_accessor_t w_arr = {.id = 0, .count = 0, .indices = NULL};
  vm_payload_t arrp = {.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  bool got_payload = vm_obj_get_payload(&arrp, &w_arr) == NULL;
  ck("get_payload on whole array gives count 4", got_payload && arrp.count == 4 && arrp.type == VM_OBJ_U32);
  uint64_t sum = 0;
  for (uint16_t i = 0; i < arrp.count; i++) {
    uint64_t el = 0;
    VM_PAYLOAD_GET_VAL(el, vm_payload_at(arrp, i));
    sum += el;
  }
  ck("iterating the payload sums 10+20+30+40", sum == 100);

  // depth cap: a by-ref chain longer than VM_ACCESSOR_MAX_DEPTH must stop
  vm_accessor_t* chain[12] = {0};
  bool built = vm_accessor_create(&chain[11], s_acc_id++, 1, 0) == NULL;
  for (int i = 10; i >= 0 && built; i--) {
    built = vm_accessor_create(&chain[i], s_acc_id++, 0, 1) == NULL && vm_accessor_set_ref(chain[i], 0, chain[i + 1]) == NULL;
  }
  ck("built a 12-deep by-ref chain", built);
  if (built) {
    got = 0;
    ck("by-ref chain past MAX_DEPTH -> DEPTH_EXCEEDED", VM_OBJ_GET_VAL(got, chain[0]) != NULL);
  }
}

/* ==========================================================================
   D -- nested objects

   Builds a real tree rather than the single parent/child pair the earlier
   stages use:

     root (PTR[2])
      +-[0]-> branch_a (PTR[2])
      |        +-[0]-> leaf_x (U32[3]) {100,200,300}
      |        +-[1]-> leaf_y (F)      2.5   tag "yval"
      +-[1]-> branch_b (PTR[1])
               +-[0]-> leaf_z (U8[4])  {1,2,3,4}  tag "zdata"

     grid (PTR[3]) -> row0/row1/row2 (U32[3])   -- the jagged 2D shape
   ========================================================================== */

#define N_ROOT 0
#define N_BR_A 1
#define N_BR_B 2
#define N_LEAF_X 3
#define N_LEAF_Y 4
#define N_LEAF_Z 5
#define N_GRID 6
#define N_ROW0 7
#define N_ROW1 8
#define N_ROW2 9

static void test_nested(void) {
  ESP_LOGI(TAG, "-- D: nested objects --");
  direct_arena_reset();

  vm_obj_h root = mk(N_ROOT, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h br_a = mk(N_BR_A, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h br_b = mk(N_BR_B, VM_OBJ_PTR, 1, NULL, true);
  vm_obj_h leaf_x = mk(N_LEAF_X, VM_OBJ_U32, 3, NULL, true);
  vm_obj_h leaf_y = mk(N_LEAF_Y, VM_OBJ_F, 1, "yval", true);
  vm_obj_h leaf_z = mk(N_LEAF_Z, VM_OBJ_U8, 4, "zdata", true);
  vm_obj_h grid = mk(N_GRID, VM_OBJ_PTR, 3, NULL, true);
  vm_obj_h row0 = mk(N_ROW0, VM_OBJ_U32, 3, NULL, true);
  vm_obj_h row1 = mk(N_ROW1, VM_OBJ_U32, 3, NULL, true);
  vm_obj_h row2 = mk(N_ROW2, VM_OBJ_U32, 3, NULL, true);
  ck("nested fixtures built", root && br_a && br_b && leaf_x && leaf_y && leaf_z && grid && row0 && row1 && row2);

  uint32_t* xv = (uint32_t*)leaf_x->payload;
  xv[0] = 100;
  xv[1] = 200;
  xv[2] = 300;
  *(float*)leaf_y->payload = 2.5f;
  for (int i = 0; i < 4; i++) leaf_z->payload[i] = (uint8_t)(i + 1);
  for (int r = 0; r < 3; r++) {
    vm_obj_h row = (r == 0) ? row0 : (r == 1) ? row1 : row2;
    uint32_t* rv = (uint32_t*)row->payload;
    for (int c = 0; c < 3; c++) rv[c] = (uint32_t)(r * 10 + c);
  }

  bool linked = vm_obj_link_direct(root, 0, br_a) == NULL && vm_obj_link_direct(root, 1, br_b) == NULL && vm_obj_link_direct(br_a, 0, leaf_x) == NULL && vm_obj_link_direct(br_a, 1, leaf_y) == NULL && vm_obj_link_direct(br_b, 0, leaf_z) == NULL && vm_obj_link_direct(grid, 0, row0) == NULL &&
                vm_obj_link_direct(grid, 1, row1) == NULL && vm_obj_link_direct(grid, 2, row2) == NULL;
  ck("tree linked", linked);
  /* Linking marks the *parent* updated -- its pointer array changed. That is
     what makes a re-pointed demux cell visible to telemetry. */
  ck("link_direct marks the parent updated", root->head.f.upd == 1 && br_a->head.f.upd == 1);

  /* Three levels of literals: each step lands on a PTR slot and is
     dereferenced because another index follows; only the last reads a value. */
  static const vm_index_t i_deep[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 1}};
  static const vm_accessor_t a_deep = {.id = N_ROOT, .count = 3, .indices = i_deep};
  uint32_t u = 0;
  ck("root[0][0][1] == 200 (3-level literal chain)", VM_OBJ_GET_VAL(u, &a_deep) == NULL && u == 200);

  // literal then name then literal -- index kinds mix freely along a chain
  static const vm_index_t i_mixed[] = {{.kind = VM_IDX_LITERAL, .value = 0}, VM_IDX_BY_NAME("yval"), {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_mixed = {.id = N_ROOT, .count = 3, .indices = i_mixed};
  float f = 0;
  ck("root[0][\"yval\"][0] == 2.5 (mixed literal/name)", VM_OBJ_GET_VAL(f, &a_mixed) == NULL && f == 2.5f);

  static const vm_index_t i_bpath[] = {{.kind = VM_IDX_LITERAL, .value = 1}, VM_IDX_BY_NAME("zdata"), {.kind = VM_IDX_LITERAL, .value = 2}};
  static const vm_accessor_t a_bpath = {.id = N_ROOT, .count = 3, .indices = i_bpath};
  u = 0;
  ck("root[1][\"zdata\"][2] == 3 (other branch)", VM_OBJ_GET_VAL(u, &a_bpath) == NULL && u == 3);

  // the jagged 2D shape: grid[row][col]
  static const vm_index_t i_grid[] = {{.kind = VM_IDX_LITERAL, .value = 1}, {.kind = VM_IDX_LITERAL, .value = 2}};
  static const vm_accessor_t a_grid = {.id = N_GRID, .count = 2, .indices = i_grid};
  u = 0;
  ck("grid[1][2] == 12 (jagged 2D)", VM_OBJ_GET_VAL(u, &a_grid) == NULL && u == 12);

  // whole-object resolve at each depth
  static const vm_index_t i_r0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_r0 = {.id = N_ROOT, .count = 1, .indices = i_r0};
  static const vm_index_t i_r00[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_r00 = {.id = N_ROOT, .count = 2, .indices = i_r00};
  vm_obj_h h = NULL;
  ck("get_obj(root[0]) -> branch_a", vm_get_obj(&h, &a_r0) == NULL && h == br_a);
  h = NULL;
  ck("get_obj(root[0][0]) -> leaf_x", vm_get_obj(&h, &a_r00) == NULL && h == leaf_x);

  /* Iterating an array that lives two levels down: resolve to the object,
     then take its whole payload. A chain cannot express "the array itself"
     because its last step would land on the PTR slot. */
  uint32_t sum = 0;
  if (vm_get_obj(&h, &a_r00) == NULL && h) {
    vm_payload_t p = vm_obj_as_payload(h);
    for (uint16_t i = 0; i < p.count; i++) {
      uint32_t el = 0;
      VM_PAYLOAD_GET_VAL(el, vm_payload_at(p, i));
      sum += el;
    }
  }
  ck("iterate nested array via get_obj -> 600", sum == 600);

  /* Writing through a deep chain reaches the real leaf. Clear upd first:
     building the tree set it on every parent, because vm_obj_link_direct()
     legitimately marks a cell whose pointer array changed. */
  root->head.f.upd = 0;
  br_a->head.f.upd = 0;
  leaf_x->head.f.upd = 0;
  uint32_t w = 999;
  ck("write through root[0][0][1]", VM_OBJ_SET_VAL(w, &a_deep) == NULL && xv[1] == 999);
  ck("deep write marks only the leaf, not the parents it traversed", leaf_x->head.f.upd == 1 && root->head.f.upd == 0 && br_a->head.f.upd == 0);
  xv[1] = 200;

  /* Aliasing: one child linked into two parents is genuinely shared, not
     copied -- a write through either path is visible from the other. */
  ck("relink leaf_x under branch_b too", vm_obj_link_direct(br_b, 0, leaf_x) == NULL);
  static const vm_index_t i_alias[] = {{.kind = VM_IDX_LITERAL, .value = 1}, {.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 1}};
  static const vm_accessor_t a_alias = {.id = N_ROOT, .count = 3, .indices = i_alias};
  u = 0;
  ck("root[1][0][1] reaches the same leaf -> 200", VM_OBJ_GET_VAL(u, &a_alias) == NULL && u == 200);
  w = 555;
  u = 0;
  ck("write via one path is visible from the other", VM_OBJ_SET_VAL(w, &a_alias) == NULL && VM_OBJ_GET_VAL(u, &a_deep) == NULL && u == 555);

  // failures at depth
  static const vm_index_t i_oob2[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 7}};
  static const vm_accessor_t a_oob2 = {.id = N_ROOT, .count = 2, .indices = i_oob2};
  ck("OOB at level 2 -> error", VM_OBJ_GET_VAL(u, &a_oob2) != NULL);

  static const vm_index_t i_far[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_far = {.id = N_ROOT, .count = 4, .indices = i_far};
  ck("chaining past a scalar leaf -> TYPE_MISMATCH", VM_OBJ_GET_VAL(u, &a_far) != NULL);

  static const vm_index_t i_badname[] = {{.kind = VM_IDX_LITERAL, .value = 0}, VM_IDX_BY_NAME("nope"), {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_badname = {.id = N_ROOT, .count = 3, .indices = i_badname};
  ck("name miss at level 2 -> NAME_NOT_FOUND", VM_OBJ_GET_VAL(f, &a_badname) != NULL);

  /* Re-linking a branch is the switch/demux primitive: consumers keep their
     wiring and silently follow the new subtree. Done last, since it changes
     what every root[0]... accessor above resolves to. */
  ck("re-point root[0] at branch_b", vm_obj_link_direct(root, 0, br_b) == NULL);
  u = 0;
  ck("root[0][0][1] now reads through branch_b", VM_OBJ_GET_VAL(u, &a_deep) == NULL && u == 555);
  ck("root[0][\"yval\"] no longer resolves after re-link", VM_OBJ_GET_VAL(f, &a_mixed) != NULL);
}

/* ==========================================================================
   E -- mutation
   ========================================================================== */

static void test_mutation(void) {
  ESP_LOGI(TAG, "-- E: mutation --");
  direct_arena_reset();

  vm_obj_h a = mk(0, VM_OBJ_U32, 4, NULL, true);
  vm_obj_h b = mk(1, VM_OBJ_U32, 4, NULL, true);
  vm_obj_h small = mk(2, VM_OBJ_U32, 2, NULL, true);
  vm_obj_h other = mk(3, VM_OBJ_F, 4, NULL, true);
  vm_obj_h ro = mk(4, VM_OBJ_U32, 4, NULL, false);
  vm_obj_h cell = mk(5, VM_OBJ_PTR, 1, NULL, true);
  vm_obj_h leaf = mk(6, VM_OBJ_F, 1, NULL, true);
  ck("fixtures built", a && b && small && other && ro && cell && leaf);

  uint32_t* av = (uint32_t*)a->payload;
  av[0] = 1;
  av[1] = 2;
  av[2] = 3;
  av[3] = 4;

  // direct writes, no accessor
  uint32_t v = 77;
  ck("set_scalar_direct writes element 2", VM_OBJ_SET_VAL_AT(v, a, 2) == NULL && av[2] == 77);
  ck("set_scalar_direct sets upd", a->head.f.upd == 1);
  ck("set_scalar_direct rejects index past end", VM_OBJ_SET_VAL_AT(v, a, 9) != NULL);
  ck("set_scalar_direct rejects non-mutable", VM_OBJ_SET_VAL_AT(v, ro, 0) != NULL);

  /* copy_content works on accessors, not handles -- count 0 means "the whole
     value", which is what a bulk copy wants. */
  static const vm_accessor_t w_a = {.id = 0, .count = 0, .indices = NULL};
  static const vm_accessor_t w_b = {.id = 1, .count = 0, .indices = NULL};
  static const vm_accessor_t w_small = {.id = 2, .count = 0, .indices = NULL};
  static const vm_accessor_t w_other = {.id = 3, .count = 0, .indices = NULL};
  static const vm_accessor_t w_ro = {.id = 4, .count = 0, .indices = NULL};
  static const vm_accessor_t w_cell = {.id = 5, .count = 0, .indices = NULL};

  b->head.f.upd = 0;
  uint32_t* bv = (uint32_t*)b->payload;
  bool copied = vm_obj_copy_content(&w_a, &w_b) == NULL;
  ck("copy_content same type+count copies every element", copied && bv[0] == 1 && bv[1] == 2 && bv[2] == 77 && bv[3] == 4);
  ck("copy_content sets upd on the destination", b->head.f.upd == 1);
  ck("copy_content count mismatch -> COPY_MISMATCH", vm_obj_copy_content(&w_a, &w_small) != NULL);
  ck("copy_content type mismatch -> COPY_MISMATCH", vm_obj_copy_content(&w_a, &w_other) != NULL);
  ck("copy_content into non-mutable -> NOT_MUTABLE", vm_obj_copy_content(&w_a, &w_ro) != NULL);
  /* Copying pointer payloads would alias two graphs onto one child, so it is
     refused on either side rather than producing a shared subtree. */
  ck("copy_content refuses VM_OBJ_PTR source", vm_obj_copy_content(&w_cell, &w_cell) != NULL);

  // link
  ck("link_direct points cell at leaf", vm_obj_link_direct(cell, 0, leaf) == NULL && *(vm_obj_h*)cell->payload == leaf);
  ck("link_direct rejects non-PTR target", vm_obj_link_direct(a, 0, leaf) != NULL);
  ck("link_direct rejects index past end", vm_obj_link_direct(cell, 5, leaf) != NULL);

  /* Accessor-addressed link -- what a switch/demux block uses when its cell
     is reached through wiring rather than held directly. The owner accessor
     must land on the pointer *slot*, hence the literal index. */
  static const vm_index_t cell0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t w_cell_slot = {.id = 5, .count = 1, .indices = cell0};
  static const vm_accessor_t w_leaf = {.id = 6, .count = 0, .indices = NULL};
  static const vm_accessor_t w_a_whole = {.id = 0, .count = 0, .indices = NULL};
  *(vm_obj_h*)cell->payload = NULL;
  ck("vm_obj_link via accessors points cell at leaf", vm_obj_link(&w_leaf, &w_cell_slot) == NULL && *(vm_obj_h*)cell->payload == leaf);
  ck("vm_obj_link rejects a non-PTR owner slot", vm_obj_link(&w_leaf, &w_a_whole) != NULL);
}

/* ==========================================================================
   F -- block API
   ========================================================================== */

static uint8_t s_blk[160];

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
    vm_block_set_ENO(block, true);
  } else {
    vm_block_set_ENO(block, false);
  }
}

static void test_block_api(void) {
  ESP_LOGI(TAG, "-- F: block API --");

  direct_arena_reset();

  vm_obj_h gate = mk(0, VM_OBJ_B, 1, NULL, true);
  vm_obj_h eno = mk(1, VM_OBJ_B, 1, NULL, true);
  vm_obj_h out = mk(2, VM_OBJ_U32, 1, NULL, true);
  vm_obj_h gate2 = mk(3, VM_OBJ_B, 1, NULL, true);
  ck("fixtures built", gate && eno && out && gate2);

  static const vm_index_t i0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t a_gate = {.id = 0, .count = 1, .indices = i0};
  static const vm_accessor_t a_gate2 = {.id = 3, .count = 1, .indices = i0};
  static const vm_accessor_t a_in = {.id = 2, .count = 1, .indices = i0};

  memset(s_blk, 0, sizeof(s_blk));
  vm_block_h blk = (vm_block_h)s_blk;
  blk->cfg.block_idx = 7;
  blk->cfg.block_type = 3;
  blk->cfg.in_cnt = 2;
  blk->cfg.q_cnt = 1;
  blk->cfg.en_cnt = 0;
  blk->cfg.eno = eno;
  vm_block_inputs(blk)[0] = &a_in;
  vm_block_inputs(blk)[1] = NULL;  // declared but unwired
  vm_block_outputs(blk)[0] = out;

  ck("vm_block_size accounts for every pin kind",
     vm_block_size(2, 1, 3, 8) == sizeof(vm_block_data_t) + 2 * sizeof(vm_accessor_t*) + 1 * sizeof(vm_obj_h) + 3 * sizeof(vm_accessor_t*) + 8);

  const vm_accessor_t* got_in = NULL;
  ck("get_in(0) returns the wired accessor", vm_block_get_in(&got_in, blk, 0) == NULL && got_in == &a_in);
  ck("get_in(1) unwired -> PIN_UNLINKED", vm_block_get_in(&got_in, blk, 1) != NULL);
  ck("get_in(5) absent -> PIN_MISSING", vm_block_get_in(&got_in, blk, 5) != NULL);

  vm_obj_h got_out = NULL;
  ck("get_out(0) returns the bound object", vm_block_get_out(&got_out, blk, 0) == NULL && got_out == out);
  ck("get_out(3) absent -> PIN_MISSING", vm_block_get_out(&got_out, blk, 3) != NULL);

  // EN semantics
  ck("no EN source reads as enabled", vm_block_is_enabled(blk));
  blk->cfg.en_cnt = 1;
  vm_block_en_list(blk)[0] = &a_gate;
  *(uint8_t*)gate->payload = 0;
  ck("EN false disables", !vm_block_is_enabled(blk));
  *(uint8_t*)gate->payload = 1;
  ck("EN true enables", vm_block_is_enabled(blk));

  /* ANY -- branches rejoining: either path reaching the block runs it. */
  blk->cfg.en_cnt = 2;
  blk->cfg.en_mode = VM_BLK_EN_ANY;
  vm_block_en_list(blk)[0] = &a_gate;
  vm_block_en_list(blk)[1] = &a_gate2;
  *(uint8_t*)gate->payload = 0;
  *(uint8_t*)gate2->payload = 0;
  ck("ANY with every source false disables", !vm_block_is_enabled(blk));
  *(uint8_t*)gate2->payload = 1;
  ck("ANY enabled by its second source", vm_block_is_enabled(blk));
  *(uint8_t*)gate->payload = 1;
  *(uint8_t*)gate2->payload = 0;
  ck("ANY enabled by its first source", vm_block_is_enabled(blk));

  /* ALL -- independent conditions: every source must hold. Same wiring, so
     the only difference is the mode. */
  blk->cfg.en_mode = VM_BLK_EN_ALL;
  ck("ALL with one source false disables", !vm_block_is_enabled(blk));
  *(uint8_t*)gate2->payload = 1;
  ck("ALL with every source true enables", vm_block_is_enabled(blk));
  *(uint8_t*)gate->payload = 0;
  ck("ALL disabled as soon as one source drops", !vm_block_is_enabled(blk));

  /* Mode is meaningless below two sources: one source behaves the same either
     way, which is what lets the editor default it without consequence. */
  blk->cfg.en_cnt = 1;
  *(uint8_t*)gate->payload = 1;
  ck("single source ignores ALL", vm_block_is_enabled(blk));
  blk->cfg.en_mode = VM_BLK_EN_ANY;
  ck("single source ignores ANY", vm_block_is_enabled(blk));
  blk->cfg.en_cnt = 2;
  blk->cfg.en_mode = VM_BLK_EN_ANY;
  *(uint8_t*)gate->payload = 1;

  /* An EN that cannot be resolved must read as disabled -- running the body
     when the gate is unknown is the more dangerous guess. Note this is the
     opposite of *absence*: en_cnt == 0 is a root and runs. */
  static const vm_accessor_t a_bad_gate = {.id = 900, .count = 0, .indices = NULL};
  blk->cfg.en_cnt = 1;
  vm_block_en_list(blk)[0] = &a_bad_gate;
  ck("unresolvable EN reads as disabled", !vm_block_is_enabled(blk));

  /* ...and one broken source must not mask a working one that would enable. */
  blk->cfg.en_cnt = 2;
  vm_block_en_list(blk)[0] = &a_bad_gate;
  vm_block_en_list(blk)[1] = &a_gate;
  *(uint8_t*)gate->payload = 1;
  ck("a broken source does not mask a working one", vm_block_is_enabled(blk));

  blk->cfg.en_cnt = 0;

  // ENO
  *(uint8_t*)eno->payload = 0;
  vm_block_set_ENO(blk, true);
  ck("set_ENO(true) writes 1", *(uint8_t*)eno->payload == 1);
  vm_block_set_ENO(blk, false);
  ck("set_ENO(false) writes 0", *(uint8_t*)eno->payload == 0);
  blk->cfg.eno = NULL;
  vm_block_set_ENO(blk, true);
  ck("set_ENO with no ENO object is a safe no-op", true);

  /* custom_data sits past *all three* arrays, so the enable list must move it
     -- checked with a non-zero en_cnt or the term would not be exercised. */
  blk->cfg.en_cnt = 2;
  ck("custom_data sits past every pin array",
     (uint8_t*)vm_block_custom_data(blk) == s_blk + sizeof(vm_block_data_t) + 2 * sizeof(vm_accessor_t*) + 1 * sizeof(vm_obj_h) + 2 * sizeof(vm_accessor_t*));
  blk->cfg.en_cnt = 0;

  // --- block_add_execute execution tests ---
  // 1. Normal scalar accessors
  vm_obj_h obj_a = mk(10, VM_OBJ_F, 1, NULL, true);
  vm_obj_h obj_b = mk(11, VM_OBJ_F, 1, NULL, true);
  vm_obj_h obj_sum = mk(12, VM_OBJ_F, 1, NULL, true);
  *(float*)obj_a->payload = 12.5f;
  *(float*)obj_b->payload = 7.5f;
  *(float*)obj_sum->payload = 0.0f;

  static const vm_index_t i_lit0[] = {{.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t acc_a = {.id = 10, .count = 1, .indices = i_lit0};
  static const vm_accessor_t acc_b = {.id = 11, .count = 1, .indices = i_lit0};

  blk->cfg.en_cnt = 0;
  blk->cfg.eno = eno;
  vm_block_inputs(blk)[0] = &acc_a;
  vm_block_inputs(blk)[1] = &acc_b;
  vm_block_outputs(blk)[0] = obj_sum;

  block_add_execute(blk);
  ck("block_add_execute scalar: 12.5 + 7.5 == 20.0", *(float*)obj_sum->payload == 20.0f && *(uint8_t*)eno->payload == 1);

  // 2. Tagged child access ("temp")
  vm_obj_h child_temp = mk(20, VM_OBJ_F, 1, "temp", true);
  *(float*)child_temp->payload = 21.5f;
  vm_obj_h ptr_msg = mk(21, VM_OBJ_PTR, 1, NULL, true);
  vm_obj_link_direct(ptr_msg, 0, child_temp);

  static const vm_index_t i_tag[] = {VM_IDX_BY_NAME("temp"), {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t acc_tag = {.id = 21, .count = 2, .indices = i_tag};

  vm_block_inputs(blk)[0] = &acc_tag;
  vm_block_inputs(blk)[1] = &acc_b;  // 7.5
  *(float*)obj_sum->payload = 0.0f;

  block_add_execute(blk);
  ck("block_add_execute tag: msg[\"temp\"][0] (21.5) + 7.5 == 29.0", *(float*)obj_sum->payload == 29.0f && *(uint8_t*)eno->payload == 1);

  // 3. 1D Array indexing (arr[2] + arr[3])
  vm_obj_h arr_floats = mk(30, VM_OBJ_F, 4, NULL, true);  // 4 floats
  float* arr_data = (float*)arr_floats->payload;
  arr_data[0] = 10.0f;
  arr_data[1] = 20.0f;
  arr_data[2] = 30.0f;
  arr_data[3] = 40.0f;

  static const vm_index_t i_lit2[] = {{.kind = VM_IDX_LITERAL, .value = 2}};
  static const vm_index_t i_lit3[] = {{.kind = VM_IDX_LITERAL, .value = 3}};
  static const vm_accessor_t acc_arr2 = {.id = 30, .count = 1, .indices = i_lit2};
  static const vm_accessor_t acc_arr3 = {.id = 30, .count = 1, .indices = i_lit3};

  vm_block_inputs(blk)[0] = &acc_arr2;
  vm_block_inputs(blk)[1] = &acc_arr3;
  *(float*)obj_sum->payload = 0.0f;

  block_add_execute(blk);
  ck("block_add_execute 1D array: arr[2] (30.0) + arr[3] (40.0) == 70.0", *(float*)obj_sum->payload == 70.0f && *(uint8_t*)eno->payload == 1);
}


/* ==========================================================================
   G -- object construction guards

   vm_obj_create() is the only place a shape is validated, and every id, type,
   count and name it sees can come straight off the wire. Each rejection below
   is therefore a boundary check, not a convenience.
   ========================================================================== */

static void test_obj_construction(void) {
  ESP_LOGI(TAG, "-- G: object construction guards --");
  direct_arena_reset();

  vm_obj_h o = NULL;
  vm_obj_head_t h = hd(VM_OBJ_NONE, 1);
  h.payload_size = 4;  // non-zero, so this tests the type and not the emptiness
  ck("create rejects VM_OBJ_NONE", vm_obj_create(&o, VM_ID_NONE, &h, NULL) != NULL && o == NULL);
  h.d.obj_t = 12;  // inside the 4-bit field, outside the width table
  ck("create rejects a type past the width table", vm_obj_create(&o, VM_ID_NONE, &h, NULL) != NULL);

  /* A zero payload would allocate a header whose payload pointer aliases the
     next allocation -- reads would return a neighbour's bytes and writes would
     overwrite them. */
  h = hd(VM_OBJ_U32, 0);
  ck("create rejects a zero payload -> OBJ_EMPTY", vm_obj_create(&o, VM_ID_NONE, &h, NULL) != NULL && o == NULL);

  /* payload_size is bytes and nothing here computed it, so a partial trailing
     element is expressible and has to be refused -- five bytes of U32 would let
     vm_obj_elem_ptr() hand back an element three bytes past the payload. */
  h = hd(VM_OBJ_U32, 2);
  h.payload_size = 5;
  ck("create rejects a payload that is not a whole number of elements", vm_obj_create(&o, VM_ID_NONE, &h, NULL) != NULL && o == NULL);

  /* An over-long name is gone by construction rather than by check: name_size
     is 4 bits, so it cannot exceed VM_OBJ_NAME_MAX. The wire still can, and
     vm_loader.c rejects it -- stage L covers that. */

  h = hd(VM_OBJ_PTR, 1);
  h.f.retentive = 1;
  ck("create rejects retentive PTR", vm_obj_create(&o, VM_ID_NONE, &h, NULL) != NULL);

  h = hd(VM_OBJ_U8, 1);
  // there is no arena parameter to reject any more -- the store owns it
  ck("create rejects a NULL out pointer", vm_obj_create(NULL, VM_ID_NONE, &h, NULL) != NULL);
  ck("create rejects a NULL head", vm_obj_create(&o, VM_ID_NONE, NULL, NULL) != NULL);

  // flags are the object's whole permission model -- none may be dropped
  o = NULL;
  h = hd(VM_OBJ_U8, 1);
  h.d.name_size = 1;
  h.f.mutable = 1;
  h.f.usr_mutable = 1;
  h.f.upd_resetable = 1;
  h.f.retentive = 1;
  ck("every flag round-trips into the header", vm_obj_create(&o, VM_ID_NONE, &h, "f") == NULL && o && o->head.f.mutable && o->head.f.usr_mutable && o->head.f.upd_resetable && o->head.f.retentive && o->head.f.tagged && o->head.f.upd == 0);

  /* Three flags are the creator's, not the caller's. Ask for all three and
     check they are overwritten anyway -- `dynamic` especially, because it
     means "heap-allocated" and a release cascade would hand free() a pointer
     into the arena. */
  vm_obj_h forced = NULL;
  h = hd(VM_OBJ_U8, 1);
  h.f.dynamic = 1;
  h.f.upd = 1;
  h.f.tagged = 1;  // claimed, but no name given
  ck("create overrides the flags a caller does not own", vm_obj_create(&forced, VM_ID_NONE, &h, NULL) == NULL && forced && forced->head.f.dynamic == 0 && forced->head.f.upd == 0 && forced->head.f.tagged == 0 && !vm_obj_is_dynamic(forced));

  vm_obj_h plain = NULL;
  h = hd(VM_OBJ_U8, 1);
  ck("an untagged object clears `tagged` and keeps name_size 0", vm_obj_create(&plain, VM_ID_NONE, &h, NULL) == NULL && plain && plain->head.f.tagged == 0 && plain->head.d.name_size == 0);

  // the longest tag a 4-bit name_size can describe
  vm_obj_h max = NULL;
  h = hd(VM_OBJ_U32, 2);
  h.d.name_size = 15;
  ck("15-char name is accepted", vm_obj_create(&max, VM_ID_NONE, &h, "abcdefghijklmno") == NULL && max);
  uint8_t tl = 0;
  const char* tag = max ? vm_obj_tag(max, &tl) : NULL;
  ck("tag reads back with its length", tag && tl == 15 && memcmp(tag, "abcdefghijklmno", 15) == 0);
  /* The name lives *after* the payload. That placement is what lets every
     value access skip a branch on `tagged`, so pin the address, not just the
     bytes -- moving it back to the front would still pass a content check. */
  ck("tag sits at payload + payload_size", tag == (const char*)(max->payload + max->head.payload_size));
  ck("total_size covers header + payload + name", vm_obj_total_size(max) == 4 + 8 + 15);

  /* The arena hands back whatever the previous program left in it, so
     vm_store_alloc() zeroes what it carves. Dirty a first object's storage,
     reset, then build over the same bytes. */
  direct_arena_reset();
  vm_obj_h dirty = mk(0, VM_OBJ_U32, 4, "z", true);
  if (dirty) memset(dirty->payload, 0xAA, 16);
  direct_arena_reset();
  vm_obj_h fresh = mk(0, VM_OBJ_U32, 4, "z", true);
  bool zeroed = fresh != NULL;
  for (int i = 0; fresh && i < 16; i++) {
    if (fresh->payload[i] != 0) zeroed = false;
  }
  ck("a fresh payload reads as zero over dirty arena bytes", zeroed);

  /* Exhaustion is a reported error, not a silent NULL: the loader has to be
     able to tell a client which object did not fit. Done last -- it leaves the
     arena consumed. */
  vm_obj_h big = NULL;
  vm_obj_head_t hbig = hd(VM_OBJ_U8, 60000);
  ck("arena exhaustion reports and leaves the handle NULL", vm_obj_create(&big, VM_ID_NONE, &hbig, NULL) != NULL && big == NULL);
  /* 65535 U8s is the largest payload_size can describe, and still far past the
     pool -- so it fails on memory rather than on the field, telling the client
     to shrink the program rather than the object. */
  hbig = hd(VM_OBJ_U8, 65535);
  ck("the largest describable shape still fails on the pool, not the field", vm_obj_create(&big, VM_ID_NONE, &hbig, NULL) != NULL);
}

/* ==========================================================================
   H -- name matching and accessor construction
   ========================================================================== */

// name index followed by the value step -- the two-index shape every by-name
// read needs, since the name lands on a PTR slot
#define NAMED_ACC(var, nm)                                                                                       \
  static const vm_index_t var##_i[] = {VM_IDX_BY_NAME(nm), {.kind = VM_IDX_LITERAL, .value = 0}}; \
  static const vm_accessor_t var = {.id = 0, .count = 2, .indices = var##_i}

static void test_names_and_accessor_build(void) {
  ESP_LOGI(TAG, "-- H: name matching / accessor construction --");
  direct_arena_reset();

  vm_obj_h bag = mk(0, VM_OBJ_PTR, 6, NULL, true);
  vm_obj_h c_temp = mk(1, VM_OBJ_U32, 1, "temp", true);
  vm_obj_h c_long = mk(2, VM_OBJ_U32, 1, "temperature", true);
  vm_obj_h c_anon = mk(3, VM_OBJ_U32, 1, NULL, true);
  vm_obj_h c_dup = mk(4, VM_OBJ_U32, 1, "temp", true);
  ck("name fixtures built", bag && c_temp && c_long && c_anon && c_dup);

  *(uint32_t*)c_temp->payload = 11;
  *(uint32_t*)c_long->payload = 22;
  *(uint32_t*)c_anon->payload = 33;
  *(uint32_t*)c_dup->payload = 44;

  /* Slots 0 and 4 stay unlinked and slot 1 holds an untagged child: the scan
     has to step over all three rather than stop or fault on them. Parsed
     message data arrives exactly this ragged. */
  bool wired = vm_obj_link_direct(bag, 1, c_anon) == NULL && vm_obj_link_direct(bag, 2, c_temp) == NULL && vm_obj_link_direct(bag, 3, c_long) == NULL && vm_obj_link_direct(bag, 5, c_dup) == NULL;
  ck("children wired with NULL and untagged slots in the way", wired);

  NAMED_ACC(a_exact, "temp");
  NAMED_ACC(a_full, "temperature");
  NAMED_ACC(a_prefix, "tem");
  NAMED_ACC(a_longer, "tempx");
  NAMED_ACC(a_empty, "");
  NAMED_ACC(a_over, "0123456789abcdefg");

  uint32_t v = 0;
  ck("exact tag matches past a NULL and an untagged slot", VM_OBJ_GET_VAL(v, &a_exact) == NULL && v == 11);
  v = 0;
  ck("a longer tag is reachable by its full name", VM_OBJ_GET_VAL(v, &a_full) == NULL && v == 22);

  /* Length is compared as well as bytes. Without that, "tem" would match
     "temp" and a script would read a neighbouring field forever without
     anything reporting a problem -- the worst possible failure shape for
     by-name access. */
  ck("a prefix of a tag does not match", VM_OBJ_GET_VAL(v, &a_prefix) != NULL);
  ck("a tag that is a prefix of the query does not match", VM_OBJ_GET_VAL(v, &a_longer) != NULL);
  ck("an empty query matches nothing", VM_OBJ_GET_VAL(v, &a_empty) != NULL);
  ck("a query longer than any tag can be is refused before the scan", VM_OBJ_GET_VAL(v, &a_over) != NULL);

  /* Two children share the tag "temp" (slots 2 and 5). The scan takes the
     first, so an ambiguous message reads deterministically rather than
     depending on wiring order. */
  v = 0;
  ck("the lowest matching slot wins when a tag repeats", VM_OBJ_GET_VAL(v, &a_exact) == NULL && v == 11);

  // ---- accessors built through the construction API, not as static structs
  vm_accessor_t* acc = NULL;
  bool made = vm_accessor_create(&acc, s_acc_id++, 0, 2) == NULL && acc && vm_accessor_set_name(acc, 0, "temperature", 11) == NULL && vm_accessor_set_literal(acc, 1, 0) == NULL;
  ck("built an accessor through the construction API", made && acc->count == 2 && acc->indices != NULL);
  v = 0;
  ck("a built accessor resolves like a static one", made && VM_OBJ_GET_VAL(v, acc) == NULL && v == 22);
  /* The wire form is neither NUL-terminated nor persistent, and
     find_child_by_name() calls strlen() on whatever it is handed. */
  ck("set_name stored a NUL-terminated copy", made && strcmp(acc->indices[0].name, "temperature") == 0);

  ck("set_literal past the declared index count -> ACC_INDEX_OOB", vm_accessor_set_literal(acc, 2, 0) != NULL);
  ck("set_ref past the declared index count -> ACC_INDEX_OOB", vm_accessor_set_ref(acc, 9, acc) != NULL);
  ck("set_ref rejects a NULL target", vm_accessor_set_ref(acc, 0, NULL) != NULL);
  ck("set_name rejects a 16-char name", vm_accessor_set_name(acc, 0, "0123456789abcdef", 16) != NULL);
  v = 0;
  ck("the rejected setters left index 0 intact", VM_OBJ_GET_VAL(v, acc) == NULL && v == 22);

  vm_accessor_t* whole = NULL;
  ck("a zero-index accessor allocates no index array", vm_accessor_create(&whole, s_acc_id++, 1, 0) == NULL && whole && whole->count == 0 && whole->indices == NULL);
  ck("setting an index on a zero-index accessor -> ACC_INDEX_OOB", vm_accessor_set_literal(whole, 0, 0) != NULL);

  /* ---- registry guards. One binding path for all three kinds now, so these
     are the checks that used to be duplicated per table. The id is validated
     before any arena is spent, which is why a rejected bind can be followed
     by a successful one of the same size. */
  void* p = NULL;
  ck("alloc rejects an id past the registry", vm_store_alloc(&p, VM_REG_OBJ, 32, 8) != NULL && p == NULL);
  ck("alloc rejects a second binding of the same id", vm_store_alloc(&p, VM_REG_OBJ, 1, 8) != NULL);
  ck("alloc rejects a bound accessor id", vm_store_alloc(&p, VM_REG_ACC, 0, 8) != NULL);
  ck("VM_ID_NONE allocates without binding", vm_store_alloc(&p, VM_REG_OBJ, VM_ID_NONE, 8) == NULL && p != NULL);
  ck("a free id binds and is then readable", vm_store_alloc(&p, VM_REG_OBJ, 20, 8) == NULL && vm_store_get(VM_REG_OBJ, 20) == p);
  ck("each registry has its own id space", vm_store_get(VM_REG_ACC, 20) == NULL && vm_store_get(VM_REG_BLK, 20) == NULL);

  /* Reset detaches instead of clearing entries: the arena they point into is
     about to be handed out again, so every id must read NULL for the whole
     window between teardown and the next successful load. */
  vm_store_reset();
  ck("after reset every object id resolves NULL", vm_obj_by_id(0) == NULL && vm_obj_by_id(1) == NULL);
  ck("after reset every accessor id resolves NULL", vm_accessor_by_id(5) == NULL);
  ck("after reset every block id resolves NULL", vm_block_by_id(0) == NULL);
  ck("resolution fails closed against a detached store", VM_OBJ_GET_VAL(v, &a_exact) != NULL);
  ck("alloc against a detached store -> REG_OOB", vm_store_alloc(&p, VM_REG_OBJ, 0, 8) != NULL);
}

/* ==========================================================================
   I -- access edge cases
   ========================================================================== */

static void test_access_edges(void) {
  ESP_LOGI(TAG, "-- I: access edge cases --");
  direct_arena_reset();

  vm_obj_h box = mk(0, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h arr = mk(1, VM_OBJ_U32, 4, "arr", true);
  vm_obj_h one = mk(2, VM_OBJ_U32, 1, NULL, true);
  vm_obj_h fsel = mk(3, VM_OBJ_F, 1, NULL, true);
  vm_obj_h isel = mk(4, VM_OBJ_I32, 1, NULL, true);
  vm_obj_h flag = mk(5, VM_OBJ_B, 1, NULL, true);
  vm_obj_h ro_box = mk(6, VM_OBJ_PTR, 1, NULL, false);
  ck("edge fixtures built", box && arr && one && fsel && isel && flag && ro_box);

  uint32_t* av = (uint32_t*)arr->payload;
  av[0] = 5;
  av[1] = 15;
  av[2] = 25;
  av[3] = 33;
  ck("link arr into box[0]", vm_obj_link_direct(box, 0, arr) == NULL);

  static const vm_accessor_t w_box = {.id = 0, .count = 0, .indices = NULL};
  static const vm_accessor_t w_arr = {.id = 1, .count = 0, .indices = NULL};
  static const vm_accessor_t w_one = {.id = 2, .count = 0, .indices = NULL};

  /* A chainless accessor names the object itself. Following box's first
     pointer here would hand a relay or encoder block child[0] instead of the
     container it was wired to, with nothing to indicate the substitution. */
  vm_obj_h h = NULL;
  ck("get_obj on a whole PTR object returns the container", vm_get_obj(&h, &w_box) == NULL && h == box);
  h = NULL;
  ck("get_obj on a whole scalar object returns it", vm_get_obj(&h, &w_arr) == NULL && h == arr);

  // a chainless write has no element to land on, so it takes the first
  uint32_t v = 42;
  ck("write via a count-0 accessor lands on element 0", VM_OBJ_SET_VAL(v, &w_arr) == NULL && av[0] == 42);

  /* Pointer payloads are not scalars. Letting a scalar write through would
     overwrite a live child address with an integer -- the resulting handle is
     then dereferenced by every later resolve. */
  ck("a scalar write into a PTR object is refused", VM_OBJ_SET_VAL(v, &w_box) != NULL);
  ck("the refused write left the link intact", *(vm_obj_h*)box->payload == arr);
  ck("set_scalar_direct into a PTR element is refused", VM_OBJ_SET_VAL_AT(v, box, 0) != NULL);

  ck("set_scalar_direct rejects a NULL object", vm_obj_set_scalar_direct(NULL, 0, (vm_val_t){.u32 = 1}, VM_OBJ_U32) != NULL);
  ck("link_direct rejects a NULL child", vm_obj_link_direct(box, 1, arr) == NULL && vm_obj_link_direct(box, 1, NULL) != NULL);
  ck("link_direct rejects a non-mutable cell", vm_obj_link_direct(ro_box, 0, arr) != NULL);

  // ---- by-ref indices: the index is data, so it arrives in any type or range
  static const vm_accessor_t w_fsel = {.id = 3, .count = 0, .indices = NULL};
  static const vm_accessor_t w_isel = {.id = 4, .count = 0, .indices = NULL};
  static const vm_index_t by_f[] = {{.kind = VM_IDX_REF, .ref = &w_fsel}};
  static const vm_accessor_t a_by_f = {.id = 1, .count = 1, .indices = by_f};
  static const vm_index_t by_i[] = {{.kind = VM_IDX_REF, .ref = &w_isel}};
  static const vm_accessor_t a_by_i = {.id = 1, .count = 1, .indices = by_i};

  *(float*)fsel->payload = 2.7f;
  uint32_t got = 0;
  ck("a float index rounds to element 3", VM_OBJ_GET_VAL(got, &a_by_f) == NULL && got == 33);

  /* A negative index reinterprets as a huge unsigned one. It must land past
     the end and be reported -- not be truncated into an in-range element. */
  *(int32_t*)isel->payload = -1;
  ck("a negative by-ref index is out of range, not wrapped", VM_OBJ_GET_VAL(got, &a_by_i) != NULL);
  /* 65536 is the interesting one: narrowing the index to 16 bits before the
     bounds check would turn it into element 0 and read a real value. */
  *(int32_t*)isel->payload = 65536;
  ck("index 65536 is out of range, not truncated to 0", VM_OBJ_GET_VAL(got, &a_by_i) != NULL);
  *(int32_t*)isel->payload = 65537;
  ck("index 65537 is out of range, not truncated to 1", VM_OBJ_GET_VAL(got, &a_by_i) != NULL);

  // ---- element-level copy_content
  static const vm_index_t e2[] = {{.kind = VM_IDX_LITERAL, .value = 2}};
  static const vm_accessor_t a_arr2 = {.id = 1, .count = 1, .indices = e2};
  ck("copy_content copies a single element into a scalar object", vm_obj_copy_content(&a_arr2, &w_one) == NULL && *(uint32_t*)one->payload == 25);
  ck("copy_content refuses element -> whole array (count mismatch)", vm_obj_copy_content(&a_arr2, &w_arr) != NULL);
  static const vm_accessor_t a_missing = {.id = 900, .count = 0, .indices = NULL};
  ck("copy_content propagates an unresolvable source", vm_obj_copy_content(&a_missing, &w_one) != NULL);

  // ---- depth cap boundary
  /* Stage C proves a far-too-deep chain fails; a limit of 1 would pass that
     test too. This pins where the edge actually is, from both sides.

     Every link in a by-ref chain feeds the next one an *index*, so the
     intermediate objects all hold 1 -- otherwise a deep chain fails on a
     bounds check before it ever reaches the depth cap. */
  vm_obj_h idx1 = mk(7, VM_OBJ_U8, 4, NULL, true);
  ck("index-source fixture built", idx1 != NULL);
  for (int i = 0; idx1 && i < 4; i++) idx1->payload[i] = 1;
  *(uint32_t*)one->payload = 2;

  vm_accessor_t* d8[8] = {0};
  bool ok8 = idx1 && vm_accessor_create(&d8[7], s_acc_id++, 2, 0) == NULL;  // reads `one`
  for (int i = 6; i >= 1 && ok8; i--) {
    ok8 = vm_accessor_create(&d8[i], s_acc_id++, 7, 1) == NULL && vm_accessor_set_ref(d8[i], 0, d8[i + 1]) == NULL;
  }
  ok8 = ok8 && vm_accessor_create(&d8[0], s_acc_id++, 1, 1) == NULL && vm_accessor_set_ref(d8[0], 0, d8[1]) == NULL;
  got = 0;
  ck("a by-ref chain exactly at MAX_DEPTH still resolves", ok8 && VM_OBJ_GET_VAL(got, d8[0]) == NULL && got == 15);

  vm_accessor_t* d9[9] = {0};
  bool ok9 = idx1 && vm_accessor_create(&d9[8], s_acc_id++, 2, 0) == NULL;
  for (int i = 7; i >= 1 && ok9; i--) {
    ok9 = vm_accessor_create(&d9[i], s_acc_id++, 7, 1) == NULL && vm_accessor_set_ref(d9[i], 0, d9[i + 1]) == NULL;
  }
  ok9 = ok9 && vm_accessor_create(&d9[0], s_acc_id++, 1, 1) == NULL && vm_accessor_set_ref(d9[0], 0, d9[1]) == NULL;
  ck("one level past MAX_DEPTH is refused", ok9 && VM_OBJ_GET_VAL(got, d9[0]) != NULL);

  // ---- the _Generic arms a block body can reach that stage B does not
  static const vm_accessor_t w_flag = {.id = 5, .count = 0, .indices = NULL};
  bool bt = true;
  uint8_t b_out = 0;
  ck("bool source stores as B and reads back 1", VM_OBJ_SET_VAL(bt, &w_flag) == NULL && VM_OBJ_GET_VAL(b_out, &w_flag) == NULL && b_out == 1);

  int8_t i8 = -7;
  int32_t i_out = 0;
  ck("int8 source keeps its sign", VM_OBJ_SET_VAL(i8, &w_isel) == NULL && VM_OBJ_GET_VAL(i_out, &w_isel) == NULL && i_out == -7);

  int16_t i16 = -300;
  i_out = 0;
  ck("int16 source keeps its sign", VM_OBJ_SET_VAL(i16, &w_isel) == NULL && VM_OBJ_GET_VAL(i_out, &w_isel) == NULL && i_out == -300);

  uint16_t u16 = 40000;
  uint32_t u_out = 0;
  ck("uint16 source widens", VM_OBJ_SET_VAL(u16, &w_one) == NULL && VM_OBJ_GET_VAL(u_out, &w_one) == NULL && u_out == 40000);

  double d = 2.5;
  float f_out = 0;
  ck("double source narrows to float", VM_OBJ_SET_VAL(d, &w_fsel) == NULL && VM_OBJ_GET_VAL(f_out, &w_fsel) == NULL && f_out == 2.5f);

  // ---- payload stepping
  vm_payload_t bp = vm_obj_as_payload(box);
  ck("payload_at steps a PTR payload by a pointer width", vm_payload_at(bp, 1).ptr == (uint8_t*)box->payload + sizeof(void*));
  uint32_t z = 7;
  VM_PAYLOAD_GET_VAL(z, vm_payload_at(vm_obj_as_payload(arr), 99));
  ck("payload_at past the end reads as 0, not as garbage", z == 0);
}

/* ==========================================================================
   Frame builder -- bytes exactly as they arrive over BLE
   ========================================================================== */

static uint8_t s_frame[256];
static size_t s_len;

static void f_begin(uint8_t class_header, uint8_t packet) {
  s_len = 0;
  s_frame[s_len++] = class_header;
  s_frame[s_len++] = packet;
}
static void f_u8(uint8_t v) {
  s_frame[s_len++] = v;
}
static void f_u16(uint16_t v) {
  s_frame[s_len++] = (uint8_t)(v & 0xFF);
  s_frame[s_len++] = (uint8_t)(v >> 8);
}
static void f_u32(uint32_t v) {
  f_u16((uint16_t)(v & 0xFFFF));
  f_u16((uint16_t)(v >> 16));
}
static void f_blob(const void* p, size_t n) {
  memcpy(s_frame + s_len, p, n);
  s_len += n;
}
static void f_str(const char* s) {
  f_blob(s, strlen(s));
}
static void f_f32(float v) {
  f_blob(&v, sizeof(v));
}
static err_h f_send(void) {
  return sys_interface_decode(s_frame, s_len);
}

#define OBJ_MSG 0
#define OBJ_TEMP 1
#define OBJ_HUM 2

/* sec_cnt joins the open packet rather than being derived: the pool is sized
   before anything loads, and a section is an allocation like everything else.
   Most stages declare none -- a program with no sections is one section over
   the whole order (see vm_exec_pass()). */
static err_h upload_open_sec(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint16_t sec_cnt, uint32_t total) {
  f_begin(VM_LOADER_CLASS_HEADER, 0x41);
  f_u16(obj_cnt);
  f_u16(acc_cnt);
  f_u16(blk_cnt);
  f_u16(sec_cnt);
  f_u32(total);
  return f_send();
}

static err_h upload_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint32_t total) {
  return upload_open_sec(obj_cnt, acc_cnt, blk_cnt, 0, total);
}

/* Call sites still read in elements, which is how a program is actually
   described; the wire carries bytes, so the conversion a client would do lives
   here. add_obj_record_raw() is for the cases that need to put a byte count on
   the wire that no element count could produce. */
static void add_obj_record_raw(uint16_t id, uint16_t payload_size, uint8_t type, uint8_t flags, const char* name) {
  f_u16(id);
  vm_obj_head_t head = {0};
  head.payload_size = payload_size;
  head.d.obj_t = type & 0x0F;
  head.d.name_size = name ? (uint8_t)strlen(name) : 0;
  head.f.mutable = (flags & VM_LOAD_F_MUTABLE) != 0;
  head.f.usr_mutable = (flags & VM_LOAD_F_USR_MUTABLE) != 0;
  head.f.upd_resetable = (flags & VM_LOAD_F_UPD_RESETABLE) != 0;
  head.f.retentive = (flags & VM_LOAD_F_RETENTIVE) != 0;
  f_blob(&head, sizeof(head));
  if (name && head.d.name_size) f_str(name);
}

static void add_obj_record(uint16_t id, uint16_t item_count, uint8_t type, uint8_t flags, const char* name) {
  add_obj_record_raw(id, (uint16_t)(item_count * vm_type_width((vm_obj_t_e)type)), type, flags, name);
}

static uint8_t s_idx[64];
static uint8_t s_idx_len;

static void ix_begin(void) {
  s_idx_len = 0;
}
static void ix_literal(uint32_t v) {
  s_idx[s_idx_len++] = VM_IDX_LITERAL;
  s_idx[s_idx_len++] = (uint8_t)(v & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 8) & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 16) & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 24) & 0xFF);
}
static void ix_ref(uint16_t acc_id) {
  s_idx[s_idx_len++] = VM_IDX_REF;
  s_idx[s_idx_len++] = (uint8_t)(acc_id & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)(acc_id >> 8);
}
static void ix_name(const char* n) {
  uint8_t l = (uint8_t)strlen(n);
  s_idx[s_idx_len++] = VM_IDX_NAME;
  s_idx[s_idx_len++] = l;
  memcpy(s_idx + s_idx_len, n, l);
  s_idx_len += l;
}
static void acc_record(uint16_t acc_id, uint16_t root_id, uint8_t idx_count) {
  f_u16(acc_id);
  f_u16(root_id);
  f_u8(idx_count);
  f_u8(s_idx_len);
  f_blob(s_idx, s_idx_len);
}

/* ==========================================================================
   K -- upload via injected frames
   ========================================================================== */

static void test_upload(void) {
  ESP_LOGI(TAG, "-- K: upload via injected frames --");

  f_begin(VM_LOADER_CLASS_HEADER, 0x40);
  ck("0x40 reset", f_send() == NULL && vm_loader_state() == VM_LOAD_EMPTY);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, "temp");
  ck("0x42 before open -> BAD_STATE", f_send() != NULL);

  ck("0x41 open", upload_open(3, 4, 0, 640) == NULL && vm_loader_state() == VM_LOAD_OPEN);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(3);
  add_obj_record(OBJ_MSG, 2, VM_OBJ_PTR, VM_LOAD_F_MUTABLE, NULL);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, "temp");
  add_obj_record(OBJ_HUM, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, "hum");
  ck("0x42 create 3 objects", f_send() == NULL);
  ck("objects reachable by id", vm_obj_by_id(OBJ_MSG) && vm_obj_by_id(OBJ_TEMP) && vm_obj_by_id(OBJ_HUM));

  f_begin(VM_LOADER_CLASS_HEADER, 0x43);
  f_u8(3);
  f_u16(OBJ_TEMP);
  f_u16(0);
  f_u16(4);
  f_f32(21.5f);
  f_u16(OBJ_HUM);
  f_u16(0);
  f_u16(4);
  f_f32(60.0f);
  f_u16(OBJ_MSG);
  f_u16(0);
  f_u16(4);
  f_u16(OBJ_TEMP);
  f_u16(OBJ_HUM);
  ck("0x43 values + PTR child links", f_send() == NULL);

  static const vm_index_t idx_named[] = {VM_IDX_BY_NAME("temp"), {.kind = VM_IDX_LITERAL, .value = 0}};
  static const vm_accessor_t acc_named = {.id = OBJ_MSG, .count = 2, .indices = idx_named};
  float got = 0.0f;
  ck("read msg[\"temp\"][0] == 21.5", VM_OBJ_GET_VAL(got, &acc_named) == NULL && got == 21.5f);

  static const vm_index_t idx_short[] = {VM_IDX_BY_NAME("temp")};
  static const vm_accessor_t acc_short = {.id = OBJ_MSG, .count = 1, .indices = idx_short};
  got = -1.0f;
  ck("msg[\"temp\"] alone yields PTR slot -> 0", VM_OBJ_GET_VAL(got, &acc_short) == NULL && got == 0.0f);
  vm_obj_h child = NULL;
  ck("vm_get_obj follows the trailing PTR", vm_get_obj(&child, &acc_short) == NULL && child == vm_obj_by_id(OBJ_TEMP));
  ck("vm_obj_find_child finds tagged child", vm_obj_find_child(vm_obj_by_id(OBJ_MSG), "temp") == vm_obj_by_id(OBJ_TEMP));
  ck("vm_obj_find_child returns NULL on missing tag", vm_obj_find_child(vm_obj_by_id(OBJ_MSG), "missing") == NULL);
  ck("vm_obj_find_child returns NULL on non-PTR parent", vm_obj_find_child(vm_obj_by_id(OBJ_TEMP), "temp") == NULL);

  // accessors uploaded as frames
  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(2);
  ix_begin();
  ix_name("hum");
  ix_literal(0);
  acc_record(0, OBJ_MSG, 2);
  ix_begin();
  acc_record(1, OBJ_TEMP, 0);
  ck("0x44 create accessors", f_send() == NULL);

  vm_accessor_t* a0 = vm_accessor_by_id(0);
  got = 0.0f;
  ck("loaded accessor resolves (name copy survived the frame)", a0 && VM_OBJ_GET_VAL(got, a0) == NULL && got == 60.0f);
  ck("same id yields the identical pointer -- sharing works", vm_accessor_by_id(0) == a0);
  ck("whole-object accessor has no indices", vm_accessor_by_id(1) && vm_accessor_by_id(1)->count == 0);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  ix_begin();
  ix_ref(900);
  acc_record(2, OBJ_MSG, 1);
  ck("0x44 forward/unknown REF -> rejected (cycles unconstructable)", f_send() != NULL);
}

/* ==========================================================================
   L -- malformed frames
   ========================================================================== */

static void test_malformed(void) {
  ESP_LOGI(TAG, "-- L: malformed frames --");

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  f_u16(OBJ_TEMP);
  f_u8(0);
  ck("truncated 0x42 -> SHORT_RECORD", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  f_u16(7);
  vm_obj_head_t head_short = {.payload_size = 1, .d = {.obj_t = VM_OBJ_F, .name_size = 12}, .f = {.mutable = 1}};
  f_blob(&head_short, sizeof(head_short));
  ck("0x42 name_len past end -> SHORT_RECORD", f_send() != NULL);

  /* The wire carries bytes, so a payload that is not a whole number of elements
     is now expressible and must be refused: 5 bytes of U32 would let
     vm_obj_elem_ptr() return element 1, whose last three bytes sit past the
     payload. */
  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record_raw(7, 5, VM_OBJ_U32, VM_LOAD_F_MUTABLE, NULL);
  ck("0x42 payload that is not a whole number of elements -> OBJ_BAD_SIZE", f_send() != NULL && vm_obj_by_id(7) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record_raw(7, 0, VM_OBJ_U32, VM_LOAD_F_MUTABLE, NULL);
  ck("0x42 zero payload -> OBJ_EMPTY", f_send() != NULL);

  /* An out-of-range type must be caught before it is used to index tables. */
  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(7, 1, 15, VM_LOAD_F_MUTABLE, NULL);
  ck("0x42 type past the table -> OBJ_BAD_TYPE, not a truncated type", f_send() != NULL && vm_obj_by_id(7) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, "dup");
  ck("0x42 duplicate id -> TABLE_DUP", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(900, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, NULL);
  ck("0x42 id past table -> TABLE_OOB", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x43);
  f_u8(1);
  f_u16(OBJ_TEMP);
  f_u16(0);
  f_u16(64);
  for (int i = 0; i < 64; i++) f_u8(0xAA);
  ck("0x43 write past end -> DATA_RANGE", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x43);
  f_u8(1);
  f_u16(OBJ_TEMP);
  f_u16(0);
  f_u16(200);
  ck("0x43 byte_len past frame -> SHORT_RECORD", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x43);
  f_u8(1);
  f_u16(OBJ_MSG);
  f_u16(0);
  f_u16(2);
  f_u16(777);
  ck("0x43 unknown child id -> UNKNOWN_ID", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  ix_begin();
  s_idx[s_idx_len++] = 0x7F;
  acc_record(3, OBJ_MSG, 1);
  ck("0x44 unknown index kind -> BAD_KIND", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  ix_begin();
  ix_literal(0);
  acc_record(3, OBJ_MSG, 3);
  ck("0x44 idx_count past idx_len -> SHORT_RECORD", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  ix_begin();
  ix_name("0123456789abcdef");
  acc_record(4, OBJ_MSG, 1);
  ck("0x44 16-char name -> NAME_TOO_LONG", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  ix_begin();
  acc_record(0, OBJ_TEMP, 0);
  ck("0x44 duplicate accessor id -> ACC_TABLE_DUP", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x4F);
  ck("unknown packet 0x4F -> UNKNOWN_PACKET", f_send() != NULL);

  ck("0x41 total_size past the hard ceiling -> TOO_BIG", upload_open(2, 0, 0, VM_STORE_MAX_POOL + 1) != NULL);
  ck("0x41 zero total_size -> TOO_BIG", upload_open(2, 0, 0, 0) != NULL);
  ck("after failed open, state is EMPTY", vm_loader_state() == VM_LOAD_EMPTY);
  ck("after failed open, ids resolve NULL", vm_obj_by_id(OBJ_TEMP) == NULL);
  ck("after failed open, accessor ids resolve NULL", vm_accessor_by_id(0) == NULL);
}

/* ==========================================================================
   J -- string objects

   VM_OBJ_STR is an array of chars: one byte per element, no terminator
   implied by the type. So a whole-object accessor gives the buffer, an
   indexed one gives a single character, and everything that works on a U8
   array works here too.
   ========================================================================== */

static void test_strings(void) {
  ESP_LOGI(TAG, "-- J: string objects --");
  direct_arena_reset();

  vm_obj_h s = mk(0, VM_OBJ_STR, 8, "s", true);
  vm_obj_h s2 = mk(1, VM_OBJ_STR, 8, NULL, true);
  vm_obj_h s_short = mk(2, VM_OBJ_STR, 4, NULL, true);
  vm_obj_h bytes = mk(3, VM_OBJ_U8, 8, NULL, true);
  ck("string fixtures built", s && s2 && s_short && bytes);

  ck("a STR element is one byte wide", vm_obj_type_size(s) == 1);
  ck("8 chars is 8 items in 8 bytes", vm_obj_items_cnt(s) == 8 && vm_obj_payload_size(s) == 8);

  memcpy(s->payload, "hello", 5);

  static const vm_index_t i1[] = {{.kind = VM_IDX_LITERAL, .value = 1}};
  static const vm_accessor_t a_s1 = {.id = 0, .count = 1, .indices = i1};
  static const vm_accessor_t w_s = {.id = 0, .count = 0, .indices = NULL};

  /* The reason this stage exists: every value-path switch has to name
     VM_OBJ_STR explicitly, and a missing case reads as 0 rather than
     failing -- silent, and indistinguishable from a genuinely empty slot. */
  uint8_t c = 0;
  ck("indexed read gives the character, not 0", VM_OBJ_GET_VAL(c, &a_s1) == NULL && c == 'e');

  char wc = 'a';
  ck("a plain char writes through as a character", VM_OBJ_SET_VAL(wc, &a_s1) == NULL && s->payload[1] == 'a');
  ck("chars round-trip through the accessor layer", VM_OBJ_GET_VAL(c, &a_s1) == NULL && c == 'a');
  s->payload[1] = 'e';

  // walking the buffer, which is how any string block reads one
  vm_payload_t sp = {.ptr = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  bool resolved = vm_obj_get_payload(&sp, &w_s) == NULL;
  ck("whole-object accessor yields the buffer", resolved && sp.type == VM_OBJ_STR && sp.count == 8);
  char out[9] = {0};
  for (uint16_t i = 0; resolved && i < sp.count; i++) {
    uint8_t ch = 0;
    VM_PAYLOAD_GET_VAL(ch, vm_payload_at(sp, i));
    out[i] = (char)ch;
  }
  ck("iterating the payload reads the text back", memcmp(out, "hello\0\0\0", 8) == 0);

  ck("a STR object is tagged like any other", s->head.f.tagged == 1 && s->head.d.name_size == 1);

  // bulk copy between equal-length strings
  static const vm_accessor_t w_s2 = {.id = 1, .count = 0, .indices = NULL};
  static const vm_accessor_t w_short = {.id = 2, .count = 0, .indices = NULL};
  static const vm_accessor_t w_bytes = {.id = 3, .count = 0, .indices = NULL};
  ck("copy_content copies a whole string", vm_obj_copy_content(&w_s, &w_s2) == NULL && memcmp(s2->payload, "hello", 5) == 0);
  ck("copy_content refuses a different length", vm_obj_copy_content(&w_s, &w_short) != NULL);
  /* STR and U8 have the same width but are different types, so the copy is
     refused -- the type is what tells a client how to render the bytes. */
  ck("copy_content refuses STR -> U8 despite equal widths", vm_obj_copy_content(&w_s, &w_bytes) != NULL);

  ck("writing past the end of a string is refused", VM_OBJ_SET_VAL_AT(wc, s, 8) != NULL);
  ck("writing the last character is accepted", VM_OBJ_SET_VAL_AT(wc, s, 7) == NULL && s->payload[7] == 'a');
}

/* ==========================================================================
   M -- resolution cache

   The cache is only ever an optimisation, so the thing worth testing is not
   that it is fast but that it is *invisible*: a cached accessor and an
   identical uncached one must agree on every answer, including the ones that
   fail. The dangerous case is a stale address, so the shapes that must NOT be
   cached are pinned here too.
   ========================================================================== */
static void test_resolution_cache(void) {
  ESP_LOGI(TAG, "-- M: resolution cache --");
  direct_arena_reset();

  vm_obj_h box = mk(0, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h arr = mk(1, VM_OBJ_U32, 4, NULL, true);
  vm_obj_h alt = mk(2, VM_OBJ_U32, 4, NULL, true);
  vm_obj_h ro = mk(3, VM_OBJ_U32, 2, NULL, false);
  ck("cache fixtures built", box && arr && alt && ro);
  if (!box || !arr || !alt || !ro) return;

  for (int i = 0; i < 4; i++) ((uint32_t*)arr->payload)[i] = (uint32_t)(100 + i);
  for (int i = 0; i < 4; i++) ((uint32_t*)alt->payload)[i] = (uint32_t)(900 + i);
  ck("link fixtures wired", vm_obj_link_direct(box, 0, arr) == NULL);

  // the two shapes that qualify
  vm_accessor_t* whole = NULL;
  vm_accessor_t* elem = NULL;
  bool built = vm_accessor_create(&whole, s_acc_id++, 1, 0) == NULL && vm_accessor_create(&elem, s_acc_id++, 1, 1) == NULL && vm_accessor_set_literal(elem, 0, 2) == NULL;
  ck("cacheable accessors built", built);
  ck("whole-object accessor caches", built && vm_accessor_cache_build(whole));
  ck("one-literal accessor caches", built && vm_accessor_cache_build(elem));

  uint32_t v = 0;
  ck("cached read matches the value", VM_OBJ_GET_VAL(v, elem) == NULL && v == 102);
  ck("cached write lands and sets upd", VM_OBJ_SET_VAL(((uint32_t)777), elem) == NULL && ((uint32_t*)arr->payload)[2] == 777 && arr->head.f.upd);

  vm_payload_t p = {0};
  ck("cached whole-object payload has the full count", vm_obj_get_payload(&p, whole) == NULL && p.count == 4 && p.ptr == arr->payload);

  /* A cached accessor must still refuse a read-only target: mutability is read
     from the object at access time, not frozen into the cache. */
  vm_accessor_t* roacc = NULL;
  bool ro_built = vm_accessor_create(&roacc, s_acc_id++, 3, 1) == NULL && vm_accessor_set_literal(roacc, 0, 0) == NULL;
  ck("read-only accessor caches", ro_built && vm_accessor_cache_build(roacc));
  ck("cached write to a non-mutable object is still refused", ro_built && VM_OBJ_SET_VAL(((uint32_t)5), roacc) != NULL);

  /* The shapes that must not be cached -- a two-level chain's address moves
     when the link moves, so caching it would hand back the old child. */
  vm_accessor_t* deep = NULL;
  bool deep_built = vm_accessor_create(&deep, s_acc_id++, 0, 2) == NULL && vm_accessor_set_literal(deep, 0, 0) == NULL && vm_accessor_set_literal(deep, 1, 1) == NULL;
  ck("two-level chain refuses to cache", deep_built && !vm_accessor_cache_build(deep));

  vm_accessor_t* named = NULL;
  bool named_built = vm_accessor_create(&named, s_acc_id++, 0, 1) == NULL && vm_accessor_set_name(named, 0, "x", 1) == NULL;
  ck("by-name index refuses to cache", named_built && !vm_accessor_cache_build(named));

  vm_accessor_t* oob = NULL;
  bool oob_built = vm_accessor_create(&oob, s_acc_id++, 1, 1) == NULL && vm_accessor_set_literal(oob, 0, 99) == NULL;
  ck("out-of-range literal refuses to cache", oob_built && !vm_accessor_cache_build(oob));
  ck("an uncached out-of-range accessor still reports OOB", oob_built && VM_OBJ_GET_VAL(v, oob) != NULL);

  /* The property the whole tier rests on: re-linking must be picked up. The
     deep chain is uncached, so it follows the new child; and a cached
     accessor pointing into the *old* child must keep reading that child,
     because that is what it names. */
  ck("relink to alt", vm_obj_link_direct(box, 0, alt) == NULL);
  uint32_t after = 0;
  ck("uncached deep chain follows the new link", deep_built && VM_OBJ_GET_VAL(after, deep) == NULL && after == 901);
  ck("cached accessor still names its own object", VM_OBJ_GET_VAL(v, elem) == NULL && v == 777);

  /* A cached accessor and a fresh uncached one must never disagree. */
  vm_accessor_t* twin = NULL;
  bool twin_built = vm_accessor_create(&twin, s_acc_id++, 1, 1) == NULL && vm_accessor_set_literal(twin, 0, 2) == NULL;
  uint32_t a = 0, b = 0;
  ck("cached and uncached agree", twin_built && VM_OBJ_GET_VAL(a, elem) == NULL && VM_OBJ_GET_VAL(b, twin) == NULL && a == b);

  // rebuilding after the object is gone must drop the entry, not keep a stale one
  ck("cache_build clears the flag when the object is gone", (vm_store_reset(), !vm_accessor_cache_build(elem)) && (elem->flags & VM_ACC_F_CACHED) == 0);
  ck("a dropped cache falls back to failing closed", VM_OBJ_GET_VAL(v, elem) != NULL);
  ck("cache_build survives NULL", !vm_accessor_cache_build(NULL));
}

/* ==========================================================================
   N -- block upload (0x41 blk_cnt + 0x45)

   Drives a block in over the wire and checks the thing that makes blocks
   different from objects: they name *other* ids, so the interesting failures
   are all about references. A block whose wiring does not resolve must be
   refused outright rather than created holding a NULL pin.
   ========================================================================== */

/* One 0x45 record: header, then in ids, out ids, en ids, private state.
   `en_acc` keeps its single-source spelling for brevity -- VM_BLOCK_NO_ID
   emits en_cnt 0 (a root), anything else emits a one-entry list. Multi-source
   merges are built as raw frames below, where the count is the point. */
static void add_block_record(uint16_t blk_id, uint16_t block_idx, uint8_t block_type, const uint16_t* ins, uint8_t in_cnt, const uint16_t* outs, uint8_t q_cnt, uint16_t en_acc, uint16_t eno_obj, const uint8_t* custom,
                             uint16_t custom_len) {
  f_begin(VM_LOADER_CLASS_HEADER, 0x45);
  f_u16(blk_id);
  f_u16(block_idx);
  f_u8(block_type);
  f_u8(in_cnt);
  f_u8(q_cnt);
  f_u8(en_acc == VM_BLOCK_NO_ID ? 0 : 1);
  f_u8(VM_BLK_EN_ANY);
  f_u8(VM_BLK_ERR_STOP);
  f_u16(custom_len);
  f_u16(eno_obj);
  for (uint8_t i = 0; i < in_cnt; i++) f_u16(ins[i]);
  for (uint8_t i = 0; i < q_cnt; i++) f_u16(outs[i]);
  if (en_acc != VM_BLOCK_NO_ID) f_u16(en_acc);
  if (custom_len) f_blob(custom, custom_len);
}

static void test_block_upload(void) {
  ESP_LOGI(TAG, "-- N: block upload --");
  vm_loader_reset();

  ck("0x41 open with blocks", upload_open(4, 3, 3, 1100) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(4);
  add_obj_record(0, 2, VM_OBJ_F, VM_LOAD_F_MUTABLE, NULL);   // in values
  add_obj_record(1, 1, VM_OBJ_F, VM_LOAD_F_MUTABLE, NULL);   // out
  add_obj_record(2, 1, VM_OBJ_B, VM_LOAD_F_MUTABLE, NULL);   // eno
  add_obj_record(3, 1, VM_OBJ_B, VM_LOAD_F_MUTABLE, NULL);   // en source
  ck("0x42 objects for the block", f_send() == NULL);

  // accessors: in0 = obj0[0], in1 = obj0[1], en = obj3 (chainless)
  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(3);
  f_u16(0); f_u16(0); f_u8(1); f_u8(5); f_u8(VM_IDX_LITERAL); f_u32(0);
  f_u16(1); f_u16(0); f_u8(1); f_u8(5); f_u8(VM_IDX_LITERAL); f_u32(1);
  f_u16(2); f_u16(3); f_u8(0); f_u8(0);
  ck("0x44 accessors for the block", f_send() == NULL);

  uint16_t ins[2] = {0, 1};
  uint16_t outs[1] = {1};
  uint8_t custom[4] = {0xDE, 0xAD, 0xBE, 0xEF};

  /* Any registered type does here -- this stage is about the record, not the
     block. Nothing describes what a type's private state should look like, so
     four arbitrary bytes load for any of them; VM_BLK_EXPR is used because it
     is the type whose custom_data has a documented meaning to contrast with. */
  add_block_record(0, 100, VM_BLK_EXPR, ins, 2, outs, 1, 2, 2, custom, sizeof(custom));
  ck("0x45 creates a block", f_send() == NULL);

  vm_block_h b = vm_block_by_id(0);
  ck("block is bound to its id", b != NULL);
  if (!b) return;

  ck("block header round-trips", b->cfg.block_idx == 100 && b->cfg.block_type == VM_BLK_EXPR && b->cfg.in_cnt == 2 && b->cfg.q_cnt == 1 && b->cfg.custom_len == 4 && b->cfg.on_error == VM_BLK_ERR_STOP);
  ck("ids resolved to pointers, not kept as ids", vm_block_inputs(b)[0] == vm_accessor_by_id(0) && vm_block_inputs(b)[1] == vm_accessor_by_id(1) && vm_block_outputs(b)[0] == vm_obj_by_id(1));
  ck("EN and ENO resolved", b->cfg.en_cnt == 1 && vm_block_en_list(b)[0] == vm_accessor_by_id(2) && b->cfg.eno == vm_obj_by_id(2));
  ck("private state arrived", memcmp(vm_block_custom_data(b), custom, sizeof(custom)) == 0);
  ck("total_size matches the shape", vm_block_total_size(b) == vm_block_size(2, 1, 1, 4));

  /* The block is wired to real objects, so it must actually work end to end --
     the point of resolving at load is that execution never touches an id. */
  ((float*)vm_obj_by_id(0)->payload)[0] = 2.5f;
  ((float*)vm_obj_by_id(0)->payload)[1] = 4.0f;
  *(uint8_t*)vm_obj_by_id(3)->payload = 1;  // EN true
  float a = 0, c = 0;
  const vm_accessor_t* pin = NULL;
  bool got = vm_block_get_in(&pin, b, 0) == NULL && VM_OBJ_GET_VAL(a, pin) == NULL;
  ck("an uploaded block reads through its own pin", got && a == 2.5f);
  ck("an uploaded block is enabled by its EN object", vm_block_is_enabled(b));
  vm_obj_h q = NULL;
  ck("uploaded block writes its output", vm_block_get_out(&q, b, 0) == NULL && VM_OBJ_SET_VAL_AT(6.5f, q, 0) == NULL && VM_OBJ_GET_VAL(c, vm_accessor_by_id(1)) == NULL);

  // references are the whole risk with blocks: every one of these must refuse
  add_block_record(1, 101, VM_BLK_EXPR, (uint16_t[]){99}, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown input accessor id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, (uint16_t[]){99}, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown output object id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, outs, 1, 99, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown EN accessor id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, outs, 1, VM_BLOCK_NO_ID, 99, NULL, 0);
  ck("unknown ENO object id -> BAD_REF", f_send() != NULL);

  ck("a refused block leaves its id unbound", vm_block_by_id(1) == NULL);

  /* NO_ID is legal on an input (the pin stays unwired and the block falls back
     to its own constant), on EN and on ENO -- but never on an output. */
  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, (uint16_t[]){VM_BLOCK_NO_ID}, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("NO_ID output -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, (uint16_t[]){0, VM_BLOCK_NO_ID}, 2, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("NO_ID input leaves the pin unwired", f_send() == NULL && vm_block_by_id(1) && vm_block_inputs(vm_block_by_id(1))[0] == vm_accessor_by_id(0) && vm_block_inputs(vm_block_by_id(1))[1] == NULL);
  const vm_accessor_t* unwired = NULL;
  ck("an unwired pin reports PIN_UNLINKED, not garbage", vm_block_get_in(&unwired, vm_block_by_id(1), 1) != NULL && unwired == NULL);
  ck("absent EN and NO_ID ENO leave both empty", vm_block_by_id(1)->cfg.en_cnt == 0 && vm_block_by_id(1)->cfg.eno == NULL);
  ck("a block with no EN is enabled by default", vm_block_is_enabled(vm_block_by_id(1)));

  /* A merge: two enable sources on one block, as a real en_cnt == 2 record.
     Accessor 2 is the chainless gate on obj3; accessor 0 reads obj0[0], a
     float, which is non-zero and so reads as enabled too. */
  f_begin(VM_LOADER_CLASS_HEADER, 0x45);
  f_u16(2); f_u16(106);
  f_u8(VM_BLK_EXPR); f_u8(2); f_u8(1); f_u8(2); f_u8(VM_BLK_EN_ANY); f_u8(VM_BLK_ERR_STOP); f_u16(0); f_u16(VM_BLOCK_NO_ID);
  f_u16(0); f_u16(1);  // inputs
  f_u16(1);            // output
  f_u16(2); f_u16(0);  // two enable sources
  ck("0x45 accepts a two-source enable list", f_send() == NULL && vm_block_by_id(2) && vm_block_by_id(2)->cfg.en_cnt == 2);
  ck("en_mode round-trips", vm_block_by_id(2)->cfg.en_mode == VM_BLK_EN_ANY);
  ck("merge resolves both sources to pointers",
     vm_block_en_list(vm_block_by_id(2))[0] == vm_accessor_by_id(2) && vm_block_en_list(vm_block_by_id(2))[1] == vm_accessor_by_id(0));
  *(uint8_t*)vm_obj_by_id(3)->payload = 0;    // first source false
  ((float*)vm_obj_by_id(0)->payload)[0] = 0;  // second source false
  ck("ANY with both sources false is disabled", !vm_block_is_enabled(vm_block_by_id(2)));
  ((float*)vm_obj_by_id(0)->payload)[0] = 2.5f;
  ck("ANY enabled by either source", vm_block_is_enabled(vm_block_by_id(2)));

  /* Same wiring, ALL mode: now one true source is no longer enough. */
  vm_block_by_id(2)->cfg.en_mode = VM_BLK_EN_ALL;
  ck("ALL with only one source true is disabled", !vm_block_is_enabled(vm_block_by_id(2)));
  *(uint8_t*)vm_obj_by_id(3)->payload = 1;
  ck("ALL enabled only once every source is true", vm_block_is_enabled(vm_block_by_id(2)));
  ((float*)vm_obj_by_id(0)->payload)[0] = 0;
  ck("ALL disabled again as soon as one source drops", !vm_block_is_enabled(vm_block_by_id(2)));
  ((float*)vm_obj_by_id(0)->payload)[0] = 2.5f;

  add_block_record(1, 102, VM_BLK_EXPR, ins, 2, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("duplicate block id -> TABLE_DUP", f_send() != NULL);

  add_block_record(9, 103, VM_BLK_EXPR, ins, 2, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("block id past the table -> TABLE_OOB", f_send() != NULL);

  // in_cnt past the connected_in mask cannot be represented, so it is refused
  uint16_t many[VM_BLOCK_MAX_IN + 1] = {0};
  add_block_record(1, 104, VM_BLK_EXPR, many, VM_BLOCK_MAX_IN + 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("in_cnt past VM_BLOCK_MAX_IN -> BAD_SHAPE", f_send() != NULL);

  // truncation: the record claims two inputs but carries one id
  f_begin(VM_LOADER_CLASS_HEADER, 0x45);
  f_u16(5); f_u16(105);
  f_u8(VM_BLK_EXPR); f_u8(2); f_u8(1); f_u8(0); f_u8(VM_BLK_EN_ANY); f_u8(VM_BLK_ERR_STOP); f_u16(0); f_u16(VM_BLOCK_NO_ID);
  f_u16(0);  // only one of the two promised ids
  ck("truncated 0x45 -> SHORT_RECORD", f_send() != NULL);

  // ...and the same for a promised enable list that is not there
  f_begin(VM_LOADER_CLASS_HEADER, 0x45);
  f_u16(5); f_u16(107);
  f_u8(VM_BLK_EXPR); f_u8(2); f_u8(1); f_u8(2); f_u8(VM_BLK_EN_ANY); f_u8(VM_BLK_ERR_STOP); f_u16(0); f_u16(VM_BLOCK_NO_ID);
  f_u16(0); f_u16(1);  // inputs
  f_u16(1);            // output
  f_u16(2);            // only one of the two promised enable ids
  ck("0x45 truncated in the enable list -> SHORT_RECORD", f_send() != NULL);

  /* The pool is heap-allocated per load, so a failed open must leave the
     program that is already running completely alone -- allocate-then-swap,
     not reset-then-allocate. Ask for more than the ceiling and check the
     loaded block is still there and still resolves. */
  ck("a rejected open leaves the running program intact",
     upload_open(4, 3, 2, VM_STORE_MAX_POOL + 1) != NULL && vm_block_by_id(0) != NULL && vm_block_by_id(0)->cfg.block_idx == 100 && vm_obj_by_id(1) != NULL);

  uint32_t before = vm_store_capacity();
  ck("a successful open re-sizes the pool to the new program", upload_open(1, 0, 0, 256) == NULL && vm_store_capacity() == 256 && before != 256);
  ck("the previous program is gone after a successful reopen", vm_block_by_id(0) == NULL);

  vm_loader_reset();
  ck("reset detaches the block registry", vm_block_by_id(0) == NULL);
  ck("reset releases the pool", vm_store_capacity() == 0);
}

/* ==========================================================================
   O -- dynamic objects

   Heap-allocated, reference counted, reachable only as a child of a PTR
   parent. Everything here is about *lifetime*, so most assertions read the
   register rather than the object: once released, the handle is freed memory
   and dereferencing it to check would be the bug the test is looking for.
   ========================================================================== */

static void test_dynamic_objects(void) {
  ESP_LOGI(TAG, "-- O: dynamic objects --");

  direct_arena_reset();
  vm_obj_dyn_reset();

  vm_obj_h d = NULL;
  vm_obj_head_t dh = hd(VM_OBJ_F, 1);
  dh.d.name_size = 4;
  dh.f.mutable = 1;
  ck("dynamic create succeeds", vm_obj_dyn_create(&d, &dh, "temp") == NULL && d);
  ck("it is flagged dynamic", d && vm_obj_is_dynamic(d));
  ck("it is in the register, owned by nobody", vm_obj_dyn_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_id(d)].ref_cnt == 0);
  ck("dyn_get round-trips the slot", vm_obj_dyn_get(vm_obj_dyn_id(d)) == d);

  // it is an ordinary object otherwise -- shape, tag and payload all work
  uint8_t tl = 0;
  const char* tag = d ? vm_obj_tag(d, &tl) : NULL;
  ck("a dynamic object carries its tag like any other", tag && tl == 4 && memcmp(tag, "temp", 4) == 0);
  if (d) *(float*)d->payload = 1.5f;
  ck("a dynamic object holds a value like any other", d && *(float*)d->payload == 1.5f);

  // arena objects are inert to all of this
  vm_obj_h arena = mk(0, VM_OBJ_U8, 1, NULL, true);
  ck("an arena object is not dynamic and not registered", arena && !vm_obj_is_dynamic(arena) && vm_obj_dyn_id(arena) == VM_DYN_NO_ID);
  vm_obj_dyn_retain(arena);
  vm_obj_dyn_release(arena);
  ck("retain/release are no-ops on an arena object", vm_obj_by_id(0) == arena);

  // refcount: two holders, so the first release must not free
  vm_obj_dyn_retain(d);
  vm_obj_dyn_retain(d);
  ck("two references counted", vm_obj_dyn_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_id(d)].ref_cnt == 2);
  vm_obj_dyn_release(d);
  ck("one release leaves it alive", vm_obj_dyn_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_id(d)].ref_cnt == 1);
  vm_obj_dyn_release(d);
  ck("the last release frees it and clears the slot", vm_obj_dyn_id(d) == VM_DYN_NO_ID);

  /* A parent releasing must take its dynamic children with it, and leave any
     arena child alone -- one tree can hold both. */
  vm_obj_h kid_a = NULL, kid_b = NULL;
  vm_obj_head_t kh = hd(VM_OBJ_U32, 1);
  kh.f.mutable = 1;
  ck("children created", vm_obj_dyn_create(&kid_a, &kh, NULL) == NULL && vm_obj_dyn_create(&kid_b, &kh, NULL) == NULL);
  vm_obj_h parent = NULL;
  vm_obj_head_t ph = hd(VM_OBJ_PTR, 3);
  ph.f.mutable = 1;
  ck("dynamic PTR parent created", vm_obj_dyn_create(&parent, &ph, NULL) == NULL && parent);
  if (parent && kid_a && kid_b && arena) {
    vm_obj_h* slots = (vm_obj_h*)parent->payload;
    slots[0] = kid_a;
    slots[1] = arena;  // an arena child in a dynamic tree
    slots[2] = kid_b;
    vm_obj_dyn_retain(kid_a);
    vm_obj_dyn_retain(kid_b);
    vm_obj_dyn_retain(arena);  // no-op, but the link path calls it unconditionally
    vm_obj_dyn_retain(parent);
  }
  ck("tree registered", vm_obj_dyn_id(parent) != VM_DYN_NO_ID && vm_obj_dyn_id(kid_a) != VM_DYN_NO_ID && vm_obj_dyn_id(kid_b) != VM_DYN_NO_ID);

  vm_obj_dyn_release(parent);
  ck("releasing the parent frees both dynamic children", vm_obj_dyn_id(parent) == VM_DYN_NO_ID && vm_obj_dyn_id(kid_a) == VM_DYN_NO_ID && vm_obj_dyn_id(kid_b) == VM_DYN_NO_ID);
  ck("the arena child survives the cascade", vm_obj_by_id(0) == arena && arena->head.f.dynamic == 0);

  // the register is a bound, and hitting it is a clean rejection
  uint16_t made = 0;
  vm_obj_head_t fh = hd(VM_OBJ_U8, 1);
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    vm_obj_h f = NULL;
    if (vm_obj_dyn_create(&f, &fh, NULL) != NULL) break;
    made++;
  }
  ck("the register fills to exactly VM_DYN_MAX", made == VM_DYN_MAX);
  vm_obj_h overflow = NULL;
  ck("one past the register -> DYN_FULL, and nothing is handed back", vm_obj_dyn_create(&overflow, &fh, NULL) != NULL && overflow == NULL);

  /* Reset ignores reference counts, so everything above -- all of it still
     held at 0 refs and unreachable from any parent -- goes anyway. */
  vm_obj_dyn_reset();
  uint16_t live = 0;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (vm_obj_dyn_get(i)) live++;
  }
  ck("reset empties the register regardless of refcounts", live == 0);

  // a rejected shape must cost neither a slot nor an allocation
  vm_obj_h bad = NULL;
  vm_obj_head_t bh = hd(VM_OBJ_F, 0);
  ck("dynamic create rejects a zero payload", vm_obj_dyn_create(&bad, &bh, NULL) != NULL && bad == NULL && vm_obj_dyn_get(0) == NULL);
}

/* ==========================================================================
   P -- the palette

   A block type is a function and nothing else, so there is exactly one thing
   the palette can refuse: a type no function claims. What a block *takes in*
   is not described anywhere yet, deliberately -- that table comes with the
   palette itself.
   ========================================================================== */

static void test_palette(void) {
  ESP_LOGI(TAG, "-- P: palette --");

  ck("a filled slot resolves to its function", vm_block_fn_for(VM_BLK_EXPR) == vm_blk_expr);
  ck("every type in the palette resolves", vm_block_fn_for(VM_BLK_EXPR_BIT) && vm_block_fn_for(VM_BLK_IF) && vm_block_fn_for(VM_BLK_SWITCH) && vm_block_fn_for(VM_BLK_FOR));

  /* Type 0 is reserved, and the reason is worth a check of its own: an unset
     `block_type` is zero, and it must not resolve to something runnable. */
  ck("VM_BLK_NONE resolves to NULL", vm_block_fn_for(VM_BLK_NONE) == NULL);
  /* Off the end of the table, expressed as the table's own length -- a
     hand-written "last type + 1" silently becomes an *occupied* slot the next
     time a block type is added, which is the one thing this must not test. */
  ck("a type past the table resolves to NULL", vm_block_fn_for((uint8_t)g_vm_blocks_cnt) == NULL);

  vm_loader_reset();
  ck("0x41 open", upload_open(2, 1, 2, 512) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(2);
  add_obj_record(0, 1, VM_OBJ_B, VM_LOAD_F_MUTABLE, NULL);
  add_obj_record(1, 1, VM_OBJ_B, VM_LOAD_F_MUTABLE, NULL);
  ck("0x42 objects", f_send() == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x44);
  f_u8(1);
  f_u16(0); f_u16(0); f_u8(0); f_u8(0);
  ck("0x44 accessor", f_send() == NULL);

  uint16_t ins[1] = {0};
  uint16_t outs[1] = {1};

  /* Refused at the packet that introduced it, rather than loading fine and
     then being silently skipped on every pass -- a program that runs, reports
     nothing, and does less than it says. */
  add_block_record(0, 0, 200, ins, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("a block_type nothing registered -> UNKNOWN_TYPE", f_send() != NULL && vm_block_by_id(0) == NULL);
  ck("a refused block costs no arena", vm_block_by_id(0) == NULL);

  add_block_record(0, 0, VM_BLK_EXPR, ins, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("a registered type is accepted", f_send() == NULL && vm_block_by_id(0) != NULL);

  /* Private state is the type's own business, so any length loads. The block
     is the only thing that knows what those bytes mean. */
  uint8_t custom[4] = {1, 2, 3, 4};
  add_block_record(1, 1, VM_BLK_EXPR, ins, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, custom, 4);
  ck("private state of any length loads -- nothing describes it yet", f_send() == NULL && vm_block_by_id(1) != NULL);
}

/* ==========================================================================
   Q -- sections

   A section says nothing about whether or when its blocks run -- every section
   runs every pass. What it says is where the pass may be interrupted, which is
   why the ranges have to partition the order rather than merely fit inside it.
   ========================================================================== */

static void test_sections(void) {
  ESP_LOGI(TAG, "-- Q: sections --");
  direct_arena_reset();

  /* Three blocks to have an order to carve up. Shape does not matter here;
     these go in through the direct API, which is below the palette check. */
  vm_obj_h q = mk(0, VM_OBJ_B, 1, NULL, true);
  uint16_t outs[1] = {0};
  for (uint16_t i = 0; i < 3; i++) {
    vm_block_h b = NULL;
    (void)vm_block_create(&b, i,
                          &(vm_block_cfg_t){.block_idx = i, .block_type = VM_BLK_EXPR, .q_cnt = 1, .out_obj_ids = outs, .eno_obj_id = VM_BLOCK_NO_ID});
  }
  ck("three blocks to section", q && vm_block_by_id(2) != NULL);

  // vm_section_count() is the declared id space, like every other registry's
  // count -- not how many are bound
  ck("a range inside the order binds", vm_section_create(0, 0, 2) == NULL && vm_section_by_id(0) != NULL);
  const vm_section_t* s0 = vm_section_by_id(0);
  ck("the range round-trips", s0 && s0->start == 0 && s0->end == 2);

  ck("an overlapping range -> SEC_OVERLAP", vm_section_create(1, 1, 3) != NULL && vm_section_by_id(1) == NULL);
  ck("the rest of the order binds", vm_section_create(1, 2, 3) == NULL);
  ck("a range past the last block -> SEC_BAD_RANGE", vm_section_create(2, 2, 9) != NULL);
  ck("an empty range -> SEC_BAD_RANGE", vm_section_create(2, 1, 1) != NULL);
  ck("a reversed range -> SEC_BAD_RANGE", vm_section_create(2, 3, 1) != NULL);
  ck("a rejected section leaves its id unbound", vm_section_by_id(2) == NULL);
}

/* ==========================================================================
   R -- the pass

   Builds a small program by the direct API and runs passes over it
   synchronously, so every assertion is about a pass that has already finished
   rather than about one happening on the other core.

   It is built out of the shipping palette -- EXPR, SWITCH and FOR -- because
   there is nothing else left to build it out of, and that is the point: there
   is no test-only block type and no test-only seam anywhere in the exec layer.
   What the supervisor promises is asserted through blocks a client could have
   compiled.

   Counting is the one thing the palette does not hand over for free. The
   placeholders used to keep a `calls` counter in their private state; the
   replacement is ex_counter() below, an EXPR that reads the object it writes
   and adds one to it. Because that object is not `upd_resetable` and starts
   life `upd`, the end-of-pass sweep never withdraws its freshness -- so the
   block triggers on every dispatch and its value is exactly how many times the
   supervisor called it.
   ========================================================================== */

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

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_custom_data(b);
  c->const_cnt = 1;
  c->code_len = sizeof(c_inc);
  c->consts[0].f = 1.0f;
  memcpy(&c->consts[1], c_inc, sizeof(c_inc));

  vm_obj_h o = vm_obj_by_id(obj);
  if (!o) return false;
  o->head.f.upd = 1;
  return true;
}

// how many times the supervisor called the counter writing `obj`
static uint32_t ex_count(uint16_t obj) {
  vm_obj_h o = vm_obj_by_id(obj);
  return o ? (uint32_t)*(float*)o->payload : 0xFFFFFFFFu;
}

static void ex_count_reset(uint16_t obj) {
  vm_obj_h o = vm_obj_by_id(obj);
  if (o) *(float*)o->payload = 0.0f;
}

static bool ex_b(uint16_t obj) {
  vm_obj_h o = vm_obj_by_id(obj);
  return o && *(uint8_t*)o->payload != 0;
}

static uint8_t ex_upd(uint16_t obj) {
  vm_obj_h o = vm_obj_by_id(obj);
  return o ? o->head.f.upd : 0xFFu;
}

static float ex_f(uint16_t obj) {
  vm_obj_h o = vm_obj_by_id(obj);
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

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_custom_data(b);
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
                                              .in_acc_ids = ins,
                                              .out_obj_ids = outs,
                                              .en_acc_ids = ens,
                                              .eno_obj_id = eno});
  if (e != NULL || b == NULL) return false;

  if (custom_len >= sizeof(vm_span_t)) {
    vm_span_t* sp = (vm_span_t*)vm_block_custom_data(b);
    sp->start = span_start;
    sp->end = span_end;
  }
  if (custom_len >= sizeof(vm_for_code_t)) {
    vm_for_code_t* c = (vm_for_code_t*)vm_block_custom_data(b);
    c->k_start = lp.start;
    c->k_end = lp.end;
    c->k_step = lp.step;
    c->max_turns = lp.budget;
    c->op = lp.op;
    c->cmp = lp.cmp;
  }
  return true;
}

static void test_exec_pass(void) {
  ESP_LOGI(TAG, "-- R: the pass --");
  vm_loader_reset();
  direct_arena_reset();

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

  *(float*)vm_obj_by_id(EX_O_DIV)->payload = 2.0f;
  vm_obj_by_id(EX_O_DIV)->head.f.upd = 1;  // never swept, so EX_FAULT runs every pass

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
  ck("...and latched that it was triggered", (vm_block_by_id(EX_TRIG)->cfg.rt & VM_BLK_RT_TRIGGERED) != 0);

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
  vm_obj_by_id(EX_O_HELD)->head.f.upd = 0;
  vm_obj_by_id(EX_ENO_TRIG)->head.f.upd = 0;
  vm_obj_by_id(EX_ENO_FAULT)->head.f.upd = 0;
  vm_obj_by_id(EX_O_BR1)->head.f.upd = 0;
  *(uint8_t*)gate->payload = 0;
  *(float*)vm_obj_by_id(EX_O_DIV)->payload = 0.0f;

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

  /* ---- sections partition the same order, and change nothing about it ---- */
  ck("sections over the whole order", vm_section_create(0, EX_TICK, EX_SPAN) == NULL && vm_section_create(1, EX_SPAN, EX_FAULT + 1) == NULL);
  ex_count_reset(EX_O_TICK);
  ex_count_reset(EX_O_INNER);
  vm_exec_pass();
  ck("every section runs every pass", ex_count(EX_O_TICK) == 1);
  ck("a span inside a section still runs from its owner only", ex_count(EX_O_INNER) == 2);

  /* That the pass timer records something, and nothing about how long.
     This stage runs a block that deliberately fails, and reporting that
     failure goes out over UART from the error handler -- which can preempt
     mid-pass and costs milliseconds per line, dwarfing the six blocks it is
     supposedly timing. The 10 ms floor is a property of the supervisor loop
     (one tick per pass), not of a pass measured with a logger in it; vm_bench.c
     is where per-access cost is actually measured. */
  ESP_LOGI(TAG, "  last pass: %lu us over 6 blocks", (unsigned long)vm_exec_last_pass_us());
  ck("the pass timer records a duration", vm_exec_last_pass_us() > 0);
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

static void test_events(void) {
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
  vm_obj_h o = vm_obj_by_id(EXPR_OUT(n));
  return o ? *(float*)o->payload : -1.0f;
}

static uint32_t out_u(uint16_t n) {
  vm_obj_h o = vm_obj_by_id(EXPR_OUT(n));
  return o ? *(uint32_t*)o->payload : 0xDEADBEEFu;
}

static bool eno_of(uint16_t n) {
  vm_obj_h o = vm_obj_by_id(EXPR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

// the block's own view of its fault episode -- VM_EXPR_RT_FAULTED
static uint8_t expr_rt(uint16_t n) {
  vm_block_h b = vm_block_by_id(n);
  return b ? ((const vm_expr_code_t*)vm_block_custom_data(b))->rt : 0xFFu;
}

static bool cfg_bad(uint16_t n) {
  vm_block_h b = vm_block_by_id(n);
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

  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_custom_data(b);
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

static void test_expr(void) {
  ESP_LOGI(TAG, "-- T: expression blocks --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 48, [VM_REG_ACC] = 8, [VM_REG_BLK] = 16, [VM_REG_SEC] = 2};
  (void)vm_store_open(DIRECT_POOL, counts);

  /* Inputs 0..4 are deliberately *not* upd_resetable, so their `upd` survives
     the end-of-pass sweep and every pass triggers -- the same standing
     freshness a constant has. Input 5 is resetable, which is what makes the
     stand-down case at the end possible. */
  bool built = true;
  for (uint16_t i = 0; i < 5; i++) {
    built = built && mk(i, i < 3 ? VM_OBJ_F : VM_OBJ_U32, 1, NULL, true) != NULL;
    if (built) vm_obj_by_id(i)->head.f.upd = 1;
  }
  vm_obj_head_t sh = hd(VM_OBJ_F, 1);
  sh.f.mutable = 1;
  sh.f.upd_resetable = 1;
  vm_obj_h stale_in = NULL;
  built = built && vm_obj_create(&stale_in, 5, &sh, NULL) == NULL;

  *(float*)vm_obj_by_id(0)->payload = 3.0f;   // A
  *(float*)vm_obj_by_id(1)->payload = 4.0f;   // B
  *(float*)vm_obj_by_id(2)->payload = 0.0f;   // the divisor, zero to begin with
  *(uint32_t*)vm_obj_by_id(3)->payload = 0x12345678u;
  *(uint32_t*)vm_obj_by_id(4)->payload = 8u;
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
  *(float*)vm_obj_by_id(2)->payload = 2.0f;
  vm_obj_by_id(EXPR_ENO(13))->head.f.upd = 0;
  vm_exec_pass();

  ck("the same expression recovers once the data does", near_f(out_f(4), 1.5f) && eno_of(4));
  ck("...and a clean evaluation re-arms fault reporting", (expr_rt(4) & VM_EXPR_RT_FAULTED) == 0);
  ck("a malformed program stays latched across passes", cfg_bad(5));

  /* An expression is a function of its inputs, so nothing arriving means the
     answer cannot have changed: the block stands down, its result stands, and
     only the flow is withdrawn. */
  ck("a stale input means the expression does not run", !eno_of(13));
  ck("...and its result stands", near_f(out_f(13), 5.0f));
  ck("...with the ENO taken false quietly", vm_obj_by_id(EXPR_ENO(13))->head.f.upd == 0);
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
  vm_obj_h o = vm_obj_by_id(BR_OUT(n, k));
  return o && *(uint8_t*)o->payload != 0;
}

static bool br_eno(uint16_t n) {
  vm_obj_h o = vm_obj_by_id(BR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

static uint8_t br_upd(uint16_t n, uint8_t k) {
  vm_obj_h o = vm_obj_by_id(BR_OUT(n, k));
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

static void test_branch(void) {
  ESP_LOGI(TAG, "-- U: flow routers --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 48, [VM_REG_ACC] = 8, [VM_REG_BLK] = 8, [VM_REG_SEC] = 2};
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

  *(float*)vm_obj_by_id(0)->payload = 1.0f;     // condition: true
  *(uint32_t*)vm_obj_by_id(1)->payload = 2u;    // selector: branch 2
  *(uint8_t*)vm_obj_by_id(2)->payload = 1;      // gate: open
  *(float*)vm_obj_by_id(3)->payload = 2.6f;     // rounds to branch 3, not 2
  *(float*)vm_obj_by_id(4)->payload = -1.0f;    // no such branch

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
  vm_block_h b0 = vm_block_by_id(0);
  ck("a router carries no payload", b0 && b0->cfg.custom_len == 0 && vm_block_total_size(b0) == vm_block_size(1, 2, 0, 0));

  /* ---- pass 1: everything decides ---- */
  vm_exec_reset_stats();
  vm_exec_pass();

  ck("a true condition takes IF", one_hot(0, 0) && br_eno(0));
  ck("a selector takes exactly its own branch", one_hot(1, 2) && br_eno(1));
  ck("an open gate lets the router decide", one_hot(2, 2) && br_eno(2));
  /* 2.6 lands on branch 3 rather than branch 2, because a float read into an
     int32 pin rounds -- vm_read_as_i32(), the same conversion every other pin
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
  *(float*)vm_obj_by_id(0)->payload = 0.0f;
  *(uint32_t*)vm_obj_by_id(1)->payload = 0u;
  *(uint8_t*)vm_obj_by_id(2)->payload = 0;
  /* These branch objects are not upd_resetable, so the end-of-pass sweep never
     touches them and pass 1's loud write is still standing. Clearing by hand is
     what makes the next two assertions about *this* pass rather than the last. */
  for (uint8_t k = 0; k < 4; k++) {
    vm_obj_by_id(BR_OUT(1, k))->head.f.upd = 0;
    vm_obj_by_id(BR_OUT(2, k))->head.f.upd = 0;
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
  *(uint32_t*)vm_obj_by_id(1)->payload = 9u;
  vm_exec_pass();

  ck("a selector past the last case takes no branch", one_hot(1, -1) && !br_eno(1));
  ck("...and still is not a config fault", !cfg_bad(1));

  /* ---- pass 4: and comes back ---- */
  *(uint32_t*)vm_obj_by_id(1)->payload = 3u;
  *(uint8_t*)vm_obj_by_id(2)->payload = 1;
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
  vm_obj_h o = vm_obj_by_id(FOR_ENO(n));
  return o && *(uint8_t*)o->payload != 0;
}

static uint32_t for_idx(uint16_t obj_id) {
  vm_obj_h o = vm_obj_by_id(obj_id);
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
  vm_block_h b = vm_block_by_id(n);
  if (!b || b->cfg.custom_len < sizeof(vm_for_code_t)) return 0xFFu;
  return ((const vm_for_code_t*)vm_block_custom_data(b))->rt;
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

static void test_for(void) {
  ESP_LOGI(TAG, "-- V: FOR and the span walk --");
  vm_loader_reset();
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 80, [VM_REG_ACC] = 16, [VM_REG_BLK] = 24, [VM_REG_SEC] = 2};
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

  *(uint32_t*)vm_obj_by_id(FOR_END)->payload = 3u;
  *(uint8_t*)vm_obj_by_id(FOR_GATE)->payload = 0;  // block 2 stays shut

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
  // room for the span, none for the loop -- claimable, unreadable
  blk = blk && ex_for(13, 14, 15, up2, NULL, 0, NULL, 0, NULL, 0, FOR_ENO(13), sizeof(vm_span_t));
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

  /* Malformed, but claimable -- so it claims. For a body that drives actuators,
     running once uncontrolled is strictly worse than not running at all. */
  ck("a malformed loop still claims, so its body stays put", for_cnt(14) == 0 && !for_eno(13));
  ck("...and is latched, so it reports once", cfg_bad(13));
  ck("...while the well-formed loops are not", !cfg_bad(0) && !cfg_bad(6) && !cfg_bad(17));

  /* The point of the whole mechanism: a body that varies per turn, and a fold
     across turns. `upd` is swept at the end of the *pass*, not per turn, so the
     accumulator reads its own last value and re-triggers -- 0+1+2+3. */
  ck("a body accumulates across turns", near_f(*(float*)vm_obj_by_id(FOR_ACC)->payload, 6.0f));

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
  *(float*)vm_obj_by_id(FOR_ACC)->payload = 0.0f;
  *(uint32_t*)vm_obj_by_id(FOR_END)->payload = 9u;
  vm_exec_pass();

  ck("a pin asking past the budget gets the budget", for_cnt(5) == 5);
  ck("...and is latched, so a stuck loop reports once", (for_rt(4) & VM_FOR_RT_BAD) != 0);
  ck("the fold repeats identically on the next pass", near_f(*(float*)vm_obj_by_id(FOR_ACC)->payload, 6.0f));

  /* ---- pass 3: back inside the budget ---- */
  for_counters_reset();
  *(uint32_t*)vm_obj_by_id(FOR_END)->payload = 2u;
  vm_exec_pass();

  ck("an end the loop can reach is honoured", for_cnt(5) == 2);
  ck("...and ending properly re-arms the report", (for_rt(4) & VM_FOR_RT_BAD) == 0);

  /* ---- pass 4: the condition is false before the first turn ---- */
  for_counters_reset();
  *(uint32_t*)vm_obj_by_id(FOR_END)->payload = 0u;
  vm_exec_pass();

  ck("a condition false at the start runs the body no times", for_cnt(5) == 0 && !for_eno(4));
  ck("...while the fixed loops are unaffected", for_cnt(1) == 3 && for_cnt(9) == 6);
}

void vm_selftest_run(void) {
  s_pass = 0;
  s_fail = 0;
  ESP_LOGW(TAG, "==== VM self-test start ====");

  /* A tick between stages, and the reason is the whole run rather than any one
     stage: this executes synchronously on `main`, which is priority 1 and
     blocks nowhere -- no assertion waits on anything and console logging does
     not yield either. So without this, IDLE0 (priority 0) never runs from boot
     init until the last stage finishes, and the task watchdog it feeds trips
     with `main` named as the CPU 0 hog. The cost is one tick per stage and it
     buys back the whole budget for whatever runs after. */
#define STAGE(fn) \
  do {            \
    fn();         \
    vTaskDelay(1); \
  } while (0)

  STAGE(test_header_helpers);
  STAGE(test_conversion);
  STAGE(test_resolution);
  STAGE(test_nested);
  STAGE(test_mutation);
  STAGE(test_block_api);
  STAGE(test_obj_construction);
  STAGE(test_names_and_accessor_build);
  STAGE(test_resolution_cache);
  STAGE(test_access_edges);
  STAGE(test_strings);
  STAGE(test_upload);
  STAGE(test_block_upload);
  STAGE(test_malformed);
  STAGE(test_dynamic_objects);
  STAGE(test_palette);
  STAGE(test_sections);
  STAGE(test_exec_pass);
  STAGE(test_events);
  STAGE(test_expr);
  STAGE(test_branch);
  STAGE(test_for);
#undef STAGE

  vm_obj_dyn_reset();  // before the loader: parents holding these live in the pool
  vm_loader_reset();

  if (s_fail == 0) {
    ESP_LOGW(TAG, "==== VM self-test: %d passed, 0 failed (scratch arena used %lu/%u B) ====", s_pass, (unsigned long)vm_store_used(), (unsigned)vm_store_capacity());
  } else {
    ESP_LOGE(TAG, "==== VM self-test: %d passed, %d FAILED ====", s_pass, s_fail);
  }
}

err_h vm_selftest_inject(const uint8_t* frame, size_t len) {
  return sys_interface_decode(frame, len);
}

