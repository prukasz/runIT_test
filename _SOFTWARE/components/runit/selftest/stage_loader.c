#include "selftest_frame.h"
#include "vm_blocks.h"
#include "vm_exec.h"

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


void test_upload(void) {
  ESP_LOGI(TAG, "-- K: upload via injected frames --");

  f_begin(VM_LOADER_CLASS_HEADER, 0x40);
  ck("0x40 reset", f_send() == NULL && vm_loader_state() == VM_LOAD_EMPTY);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, "temp");
  ck("0x42 before open -> BAD_STATE", f_send() != NULL);

  ck("0x41 open", upload_open(3, 4, 0, 640) == NULL && vm_loader_state() == VM_LOAD_OPEN);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(3);
  add_obj_record(OBJ_MSG, 2, VM_OBJ_PTR, VM_OBJ_F_MUTABLE, NULL);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, "temp");
  add_obj_record(OBJ_HUM, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, "hum");
  ck("0x42 create 3 objects", f_send() == NULL);
  ck("objects reachable by id", vm_obj_get_by_id(OBJ_MSG) && vm_obj_get_by_id(OBJ_TEMP) && vm_obj_get_by_id(OBJ_HUM));

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
  ck("vm_get_obj follows the trailing PTR", vm_obj_get_obj(&child, &acc_short) == NULL && child == vm_obj_get_by_id(OBJ_TEMP));
  ck("vm_obj_get_child finds tagged child", vm_obj_get_child(vm_obj_get_by_id(OBJ_MSG), "temp") == vm_obj_get_by_id(OBJ_TEMP));
  ck("vm_obj_get_child returns NULL on missing tag", vm_obj_get_child(vm_obj_get_by_id(OBJ_MSG), "missing") == NULL);
  ck("vm_obj_get_child returns NULL on non-PTR parent", vm_obj_get_child(vm_obj_get_by_id(OBJ_TEMP), "temp") == NULL);

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

  vm_accessor_t* a0 = vm_accessor_get_by_id(0);
  got = 0.0f;
  ck("loaded accessor resolves (name copy survived the frame)", a0 && VM_OBJ_GET_VAL(got, a0) == NULL && got == 60.0f);
  ck("same id yields the identical pointer -- sharing works", vm_accessor_get_by_id(0) == a0);
  ck("whole-object accessor has no indices", vm_accessor_get_by_id(1) && vm_accessor_get_by_id(1)->count == 0);

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

void test_malformed(void) {
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
  add_obj_record_raw(7, 5, VM_OBJ_U32, VM_OBJ_F_MUTABLE, NULL);
  ck("0x42 payload that is not a whole number of elements -> OBJ_BAD_SIZE", f_send() != NULL && vm_obj_get_by_id(7) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record_raw(7, 0, VM_OBJ_U32, VM_OBJ_F_MUTABLE, NULL);
  ck("0x42 zero payload -> OBJ_EMPTY", f_send() != NULL);

  /* An out-of-range type must be caught before it is used to index tables. */
  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(7, 1, 15, VM_OBJ_F_MUTABLE, NULL);
  ck("0x42 type past the table -> OBJ_BAD_TYPE, not a truncated type", f_send() != NULL && vm_obj_get_by_id(7) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(OBJ_TEMP, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, "dup");
  ck("0x42 duplicate id -> TABLE_DUP", f_send() != NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(1);
  add_obj_record(900, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, NULL);
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
  ck("after failed open, ids resolve NULL", vm_obj_get_by_id(OBJ_TEMP) == NULL);
  ck("after failed open, accessor ids resolve NULL", vm_accessor_get_by_id(0) == NULL);
}

void test_block_upload(void) {
  ESP_LOGI(TAG, "-- N: block upload --");
  vm_loader_reset();

  ck("0x41 open with blocks", upload_open(4, 3, 3, 1100) == NULL);

  f_begin(VM_LOADER_CLASS_HEADER, 0x42);
  f_u8(4);
  add_obj_record(0, 2, VM_OBJ_F, VM_OBJ_F_MUTABLE, NULL);   // in values
  add_obj_record(1, 1, VM_OBJ_F, VM_OBJ_F_MUTABLE, NULL);   // out
  add_obj_record(2, 1, VM_OBJ_B, VM_OBJ_F_MUTABLE, NULL);   // eno
  add_obj_record(3, 1, VM_OBJ_B, VM_OBJ_F_MUTABLE, NULL);   // en source
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

  vm_block_h b = vm_block_get_by_id(0);
  ck("block is bound to its id", b != NULL);
  if (!b) return;

  ck("block header round-trips", b->cfg.block_idx == 100 && b->cfg.block_type == VM_BLK_EXPR && b->cfg.in_cnt == 2 && b->cfg.q_cnt == 1 && b->cfg.custom_len == 4 && b->cfg.on_error == VM_BLK_ERR_STOP);
  ck("ids resolved to pointers, not kept as ids", vm_block_get_inputs(b)[0] == vm_accessor_get_by_id(0) && vm_block_get_inputs(b)[1] == vm_accessor_get_by_id(1) && vm_block_get_outputs(b)[0] == vm_obj_get_by_id(1));
  ck("EN and ENO resolved", b->cfg.en_cnt == 1 && vm_block_get_en_list(b)[0] == vm_accessor_get_by_id(2) && b->cfg.eno == vm_obj_get_by_id(2));
  ck("private state arrived", memcmp(vm_block_get_custom_data(b), custom, sizeof(custom)) == 0);
  ck("total_size matches the shape", vm_block_get_total_size(b) == vm_block_calc_size(2, 1, 1, 4));

  /* The block is wired to real objects, so it must actually work end to end --
     the point of resolving at load is that execution never touches an id. */
  ((float*)vm_obj_get_by_id(0)->payload)[0] = 2.5f;
  ((float*)vm_obj_get_by_id(0)->payload)[1] = 4.0f;
  *(uint8_t*)vm_obj_get_by_id(3)->payload = 1;  // EN true
  float a = 0, c = 0;
  const vm_accessor_t* pin = NULL;
  bool got = vm_block_get_in(&pin, b, 0) == NULL && VM_OBJ_GET_VAL(a, pin) == NULL;
  ck("an uploaded block reads through its own pin", got && a == 2.5f);
  ck("an uploaded block is enabled by its EN object", vm_block_is_enabled(b));
  vm_obj_h q = NULL;
  ck("uploaded block writes its output", vm_block_get_out(&q, b, 0) == NULL && VM_OBJ_SET_VAL_AT(6.5f, q, 0) == NULL && VM_OBJ_GET_VAL(c, vm_accessor_get_by_id(1)) == NULL);

  // references are the whole risk with blocks: every one of these must refuse
  add_block_record(1, 101, VM_BLK_EXPR, (uint16_t[]){99}, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown input accessor id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, (uint16_t[]){99}, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown output object id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, outs, 1, 99, VM_BLOCK_NO_ID, NULL, 0);
  ck("unknown EN accessor id -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, outs, 1, VM_BLOCK_NO_ID, 99, NULL, 0);
  ck("unknown ENO object id -> BAD_REF", f_send() != NULL);

  ck("a refused block leaves its id unbound", vm_block_get_by_id(1) == NULL);

  /* NO_ID is legal on an input (the pin stays unwired and the block falls back
     to its own constant), on EN and on ENO -- but never on an output. */
  add_block_record(1, 101, VM_BLK_EXPR, ins, 2, (uint16_t[]){VM_BLOCK_NO_ID}, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("NO_ID output -> BAD_REF", f_send() != NULL);

  add_block_record(1, 101, VM_BLK_EXPR, (uint16_t[]){0, VM_BLOCK_NO_ID}, 2, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("NO_ID input leaves the pin unwired", f_send() == NULL && vm_block_get_by_id(1) && vm_block_get_inputs(vm_block_get_by_id(1))[0] == vm_accessor_get_by_id(0) && vm_block_get_inputs(vm_block_get_by_id(1))[1] == NULL);
  const vm_accessor_t* unwired = NULL;
  ck("an unwired pin reports PIN_UNLINKED, not garbage", vm_block_get_in(&unwired, vm_block_get_by_id(1), 1) != NULL && unwired == NULL);
  ck("absent EN and NO_ID ENO leave both empty", vm_block_get_by_id(1)->cfg.en_cnt == 0 && vm_block_get_by_id(1)->cfg.eno == NULL);
  ck("a block with no EN is enabled by default", vm_block_is_enabled(vm_block_get_by_id(1)));

  /* A merge: two enable sources on one block, as a real en_cnt == 2 record.
     Accessor 2 is the chainless gate on obj3; accessor 0 reads obj0[0], a
     float, which is non-zero and so reads as enabled too. */
  f_begin(VM_LOADER_CLASS_HEADER, 0x45);
  f_u16(2); f_u16(106);
  f_u8(VM_BLK_EXPR); f_u8(2); f_u8(1); f_u8(2); f_u8(VM_BLK_EN_ANY); f_u8(VM_BLK_ERR_STOP); f_u16(0); f_u16(VM_BLOCK_NO_ID);
  f_u16(0); f_u16(1);  // inputs
  f_u16(1);            // output
  f_u16(2); f_u16(0);  // two enable sources
  ck("0x45 accepts a two-source enable list", f_send() == NULL && vm_block_get_by_id(2) && vm_block_get_by_id(2)->cfg.en_cnt == 2);
  ck("en_mode round-trips", vm_block_get_by_id(2)->cfg.en_mode == VM_BLK_EN_ANY);
  ck("merge resolves both sources to pointers",
     vm_block_get_en_list(vm_block_get_by_id(2))[0] == vm_accessor_get_by_id(2) && vm_block_get_en_list(vm_block_get_by_id(2))[1] == vm_accessor_get_by_id(0));
  *(uint8_t*)vm_obj_get_by_id(3)->payload = 0;    // first source false
  ((float*)vm_obj_get_by_id(0)->payload)[0] = 0;  // second source false
  ck("ANY with both sources false is disabled", !vm_block_is_enabled(vm_block_get_by_id(2)));
  ((float*)vm_obj_get_by_id(0)->payload)[0] = 2.5f;
  ck("ANY enabled by either source", vm_block_is_enabled(vm_block_get_by_id(2)));

  /* Same wiring, ALL mode: now one true source is no longer enough. */
  vm_block_get_by_id(2)->cfg.en_mode = VM_BLK_EN_ALL;
  ck("ALL with only one source true is disabled", !vm_block_is_enabled(vm_block_get_by_id(2)));
  *(uint8_t*)vm_obj_get_by_id(3)->payload = 1;
  ck("ALL enabled only once every source is true", vm_block_is_enabled(vm_block_get_by_id(2)));
  ((float*)vm_obj_get_by_id(0)->payload)[0] = 0;
  ck("ALL disabled again as soon as one source drops", !vm_block_is_enabled(vm_block_get_by_id(2)));
  ((float*)vm_obj_get_by_id(0)->payload)[0] = 2.5f;

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
     upload_open(4, 3, 2, VM_STORE_MAX_POOL + 1) != NULL && vm_block_get_by_id(0) != NULL && vm_block_get_by_id(0)->cfg.block_idx == 100 && vm_obj_get_by_id(1) != NULL);

  uint32_t before = vm_store_capacity();
  ck("a successful open re-sizes the pool to the new program", upload_open(1, 0, 0, 256) == NULL && vm_store_capacity() == 256 && before != 256);
  ck("the previous program is gone after a successful reopen", vm_block_get_by_id(0) == NULL);

  vm_loader_reset();
  ck("reset detaches the block registry", vm_block_get_by_id(0) == NULL);
  ck("reset releases the pool", vm_store_capacity() == 0);
}

/* ==========================================================================
   O -- dynamic objects

   Heap-allocated, reference counted, reachable only as a child of a PTR
   parent. Everything here is about *lifetime*, so most assertions read the
   register rather than the object: once released, the handle is freed memory
   and dereferencing it to check would be the bug the test is looking for.
   ========================================================================== */

void test_dynamic_objects(void) {
  ESP_LOGI(TAG, "-- O: dynamic objects --");

  direct_arena_reset();
  vm_obj_dyn_reset();

  vm_obj_h d = NULL;
  vm_obj_head_t dh = hd(VM_OBJ_F, 1);
  dh.d.name_size = 4;
  dh.f.mutable = 1;
  ck("dynamic create succeeds", vm_obj_dyn_create(&d, &dh, "temp") == NULL && d);
  ck("it is flagged dynamic", d && vm_obj_is_dynamic(d));
  ck("it is in the register, owned by nobody", vm_obj_dyn_get_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_get_id(d)].ref_cnt == 0);
  ck("dyn_get round-trips the slot", vm_obj_dyn_get_by_id(vm_obj_dyn_get_id(d)) == d);

  // it is an ordinary object otherwise -- shape, tag and payload all work
  uint8_t tl = 0;
  const char* tag = d ? vm_obj_get_tag(d, &tl) : NULL;
  ck("a dynamic object carries its tag like any other", tag && tl == 4 && memcmp(tag, "temp", 4) == 0);
  if (d) *(float*)d->payload = 1.5f;
  ck("a dynamic object holds a value like any other", d && *(float*)d->payload == 1.5f);

  // arena objects are inert to all of this
  vm_obj_h arena = mk(0, VM_OBJ_U8, 1, NULL, true);
  ck("an arena object is not dynamic and not registered", arena && !vm_obj_is_dynamic(arena) && vm_obj_dyn_get_id(arena) == VM_DYN_NO_ID);
  vm_obj_dyn_retain(arena);
  vm_obj_dyn_release(arena);
  ck("retain/release are no-ops on an arena object", vm_obj_get_by_id(0) == arena);

  // refcount: two holders, so the first release must not free
  vm_obj_dyn_retain(d);
  vm_obj_dyn_retain(d);
  ck("two references counted", vm_obj_dyn_get_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_get_id(d)].ref_cnt == 2);
  vm_obj_dyn_release(d);
  ck("one release leaves it alive", vm_obj_dyn_get_id(d) != VM_DYN_NO_ID && g_vm_dyn[vm_obj_dyn_get_id(d)].ref_cnt == 1);
  uint16_t d_id = vm_obj_dyn_get_id(d);
  vm_obj_dyn_release(d);
  ck("the last release frees it and clears the slot", vm_obj_dyn_get_by_id(d_id) == NULL);

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
  ck("tree registered", vm_obj_dyn_get_id(parent) != VM_DYN_NO_ID && vm_obj_dyn_get_id(kid_a) != VM_DYN_NO_ID && vm_obj_dyn_get_id(kid_b) != VM_DYN_NO_ID);

  uint16_t parent_id = vm_obj_dyn_get_id(parent), kid_a_id = vm_obj_dyn_get_id(kid_a), kid_b_id = vm_obj_dyn_get_id(kid_b);
  vm_obj_dyn_release(parent);
  ck("releasing the parent frees both dynamic children", !vm_obj_dyn_get_by_id(parent_id) && !vm_obj_dyn_get_by_id(kid_a_id) && !vm_obj_dyn_get_by_id(kid_b_id));
  ck("the arena child survives the cascade", vm_obj_get_by_id(0) == arena && arena->head.f.dynamic == 0);

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
    if (vm_obj_dyn_get_by_id(i)) live++;
  }
  ck("reset empties the register regardless of refcounts", live == 0);

  // a rejected shape must cost neither a slot nor an allocation
  vm_obj_h bad = NULL;
  vm_obj_head_t bh = hd(VM_OBJ_F, 0);
  ck("dynamic create rejects a zero payload", vm_obj_dyn_create(&bad, &bh, NULL) != NULL && bad == NULL && vm_obj_dyn_get_by_id(0) == NULL);
}

/* ==========================================================================
   P -- the palette

   A block type is a function and nothing else, so there is exactly one thing
   the palette can refuse: a type no function claims. What a block *takes in*
   is not described anywhere yet, deliberately -- that table comes with the
   palette itself.
   ========================================================================== */

void test_palette(void) {
  ESP_LOGI(TAG, "-- P: palette --");

  ck("a filled slot resolves to its function", vm_block_fn_for(VM_BLK_EXPR) != NULL);
  ck("every type in the palette resolves", vm_block_fn_for(VM_BLK_EXPR_BIT) && vm_block_fn_for(VM_BLK_IF) && vm_block_fn_for(VM_BLK_SWITCH) && vm_block_fn_for(VM_BLK_FOR) && vm_block_fn_for(VM_BLK_SET) && vm_block_fn_for(VM_BLK_CLONE));

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
  add_obj_record(0, 1, VM_OBJ_B, VM_OBJ_F_MUTABLE, NULL);
  add_obj_record(1, 1, VM_OBJ_B, VM_OBJ_F_MUTABLE, NULL);
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
  ck("a block_type nothing registered -> UNKNOWN_TYPE", f_send() != NULL && vm_block_get_by_id(0) == NULL);
  ck("a refused block costs no arena", vm_block_get_by_id(0) == NULL);

  add_block_record(0, 0, VM_BLK_EXPR, ins, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, NULL, 0);
  ck("a registered type is accepted", f_send() == NULL && vm_block_get_by_id(0) != NULL);

  /* Private state is the type's own business, so any length loads. The block
     is the only thing that knows what those bytes mean. */
  uint8_t custom[4] = {1, 2, 3, 4};
  add_block_record(1, 1, VM_BLK_EXPR, ins, 1, outs, 1, VM_BLOCK_NO_ID, VM_BLOCK_NO_ID, custom, 4);
  ck("private state of any length loads -- nothing describes it yet", f_send() == NULL && vm_block_get_by_id(1) != NULL);
}

