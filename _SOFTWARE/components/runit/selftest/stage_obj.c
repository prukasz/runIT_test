#include "selftest_harness.h"
#include "vm_block.h"

static uint16_t s_acc_id = 0;

void direct_arena_reset_obj(void) {
  direct_arena_reset();
  s_acc_id = 0;
}
#define direct_arena_reset direct_arena_reset_obj

/* ==========================================================================
   A -- header helpers and the type tables
   ========================================================================== */

void test_header_helpers(void) {
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

  /* The two tables have to agree with each other: items_cnt() divides by
     shifting while type_width() reads the byte count, so a row where
     1 << shift is not the width would leave the object layer disagreeing with
     itself. An array element is not an integer constant expression in C, so a
     _Static_assert cannot do it.

     The second half is the safety property: `obj_t` is 4 bits, vm_type_shift()
     indexes the table with all 16 of them, and the seven unused rows must read
     as 0 rather than as whatever follows the array. */
  bool tables_ok = true;
  for (unsigned t = 0; t < sizeof(vm_obj_type_shifts) / sizeof(vm_obj_type_shifts[0]); t++) {
    if (vm_type_ok((uint8_t)t)) {
      if ((1u << vm_obj_type_shifts[t]) != vm_obj_type_sizes[t]) tables_ok = false;
    } else if (vm_type_shift((uint8_t)t) != 0) {
      tables_ok = false;
    }
  }
  ck("shift and width tables agree, and every unused encoding reads as 0", tables_ok);
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

void test_conversion(void) {
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

void test_resolution(void) {
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

void test_nested(void) {
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

void test_mutation(void) {
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
  /* A pointer payload is walked rather than copied, so two trees never come to
     share a child. A cell copied onto itself has nothing to move and says so
     quietly; a pointer against a value is still a mismatch. Stage W covers the
     walk itself, where there is a tree to walk. */
  ck("copy_content on a cell copied onto itself is a no-op", vm_obj_copy_content(&w_cell, &w_cell) == NULL);
  ck("copy_content still refuses a pointer against a value",
     vm_obj_copy_content(&w_cell, &w_a) != NULL && vm_obj_copy_content(&w_a, &w_cell) != NULL);

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

void test_block_api(void) {
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

void test_obj_construction(void) {
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
  h.f.upd_resetable = 1;
  h.f.retentive = 1;
  ck("every flag round-trips into the header", vm_obj_create(&o, VM_ID_NONE, &h, "f") == NULL && o && o->head.f.mutable && o->head.f.upd_resetable && o->head.f.retentive && o->head.f.tagged && o->head.f.upd == 0);

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

void test_names_and_accessor_build(void) {
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

void test_access_edges(void) {
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
   VM_OBJ_STR is an array of chars: one byte per element, no terminator
   implied by the type. So a whole-object accessor gives the buffer, an
   indexed one gives a single character, and everything that works on a U8
   array works here too.
   ========================================================================== */

void test_strings(void) {
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
void test_resolution_cache(void) {
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

  // A program's accessor is freed with its arena. Use a caller-owned copy to
  // exercise rebuilding, rather than dereferencing the reclaimed accessor.
  vm_accessor_t detached = *elem;
  vm_index_t detached_index = elem->indices[0];
  detached.indices = &detached_index;
  ck("cache_build clears the flag when the object is gone", (vm_store_reset(), !vm_accessor_cache_build(&detached)) && (detached.flags & VM_ACC_F_CACHED) == 0);
  ck("a dropped cache falls back to failing closed", VM_OBJ_GET_VAL(v, &detached) != NULL);
  ck("cache_build survives NULL", !vm_accessor_cache_build(NULL));
}

