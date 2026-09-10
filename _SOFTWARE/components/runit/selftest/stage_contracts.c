#include "selftest_harness.h"
#include "vm_block_build.h"
#include "vm_block_clone.h"
#include "vm_block_set.h"
#include "vm_exec.h"
#include "vm_loader.h"
#include "vm_sub.h"

static uint16_t live_objects(void) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) n += vm_obj_dyn_get_by_id(i) != NULL;
  return n;
}

static vm_obj_h dynamic(vm_obj_t_e type, uint16_t n) {
  vm_obj_h obj = NULL;
  vm_obj_head_t h = vm_make_obj_head(type, n, VM_OBJ_F_MUTABLE | VM_OBJ_F_UPD_RESETABLE, 0);
  ck("dynamic fixture created", vm_obj_dyn_create(&obj, &h, NULL) == NULL);
  return obj;
}

static vm_obj_h child(vm_obj_h obj, uint16_t i) {
  return ((vm_obj_h *)obj->payload)[i];
}

static uint32_t value(vm_obj_h obj) {
  uint32_t v;
  memcpy(&v, obj->payload, sizeof(v));
  return v;
}

void test_object_contracts(void) {
  ESP_LOGI(TAG, "-- object ownership / schema / mutation contracts --");
  direct_arena_reset();
  vm_obj_h a = dynamic(VM_OBJ_PTR, 2);
  vm_obj_h b = dynamic(VM_OBJ_PTR, 1);
  vm_obj_h leaf = dynamic(VM_OBJ_U32, 1);
  ck("shared dynamic child links", !vm_obj_link_direct(a, 0, leaf) && !vm_obj_link_direct(b, 0, leaf));
  ck("sharing counts each owning slot", g_vm_dyn[vm_obj_dyn_get_id(leaf)].ref_cnt == 2);
  ck("dynamic parent links", !vm_obj_link_direct(a, 1, b));
  a->head.f.upd = b->head.f.upd = 0;
  uint16_t leaf_id = vm_obj_dyn_get_id(leaf);
  ck("back edge rejected before replacing a child", vm_obj_link_direct(b, 0, a) != NULL && child(b, 0) == leaf);
  ck("rejected link preserves counts and freshness", g_vm_dyn[leaf_id].ref_cnt == 2 && !b->head.f.upd);
  ck("self ownership rejected", vm_obj_link_direct(a, 0, a) != NULL && child(a, 0) == leaf);
  vm_obj_dyn_release(a);
  ck("shared DAG released completely", live_objects() == 0);

  // Grow from the tail, so validation must include pre-existing ancestors.
  vm_obj_h chain[VM_DYN_MAX_DEPTH + 1];
  for (uint16_t i = 0; i <= VM_DYN_MAX_DEPTH; i++) chain[i] = dynamic(VM_OBJ_PTR, 1);
  bool linked = true;
  for (uint16_t i = 0; i + 1 < VM_DYN_MAX_DEPTH; i++) linked &= vm_obj_link_direct(chain[i], 0, chain[i + 1]) == NULL;
  ck("ownership path at the limit is accepted", linked);
  ck("growing a deep path from its tail is refused", vm_obj_link_direct(chain[VM_DYN_MAX_DEPTH - 1], 0, chain[VM_DYN_MAX_DEPTH]) != NULL);
  ck("depth refusal leaves the slot empty", child(chain[VM_DYN_MAX_DEPTH - 1], 0) == NULL);
  vm_obj_dyn_release(chain[0]);
  vm_obj_dyn_release(chain[VM_DYN_MAX_DEPTH]);
  ck("depth bound never strands children on release", live_objects() == 0);

  vm_obj_h source = mk(0, VM_OBJ_U32, 1, "temp", true);
  vm_obj_h renamed = mk(1, VM_OBJ_U32, 1, "pressure", true);
  vm_obj_h holder = mk(2, VM_OBJ_PTR, 1, NULL, true);
  VM_OBJ_SET_VAL_AT((uint32_t)42, source, 0);
  VM_OBJ_SET_VAL_AT((uint32_t)99, renamed, 0);
  vm_accessor_t src = {.id = 0}, dst = {.id = 2}, other = {.id = 1};
  ck("storage compatibility ignores names", vm_obj_shape_matches(source, renamed));
  ck("schema compatibility includes names", !vm_obj_schema_matches(source, renamed));
  ck("first Clone publishes complete data", !vm_obj_clone_into(&src, &dst) && value(child(holder, 0)) == 42);
  vm_obj_h first = child(holder, 0);
  holder->head.f.upd = 0;
  ck("same schema reuses storage and publishes holder", !vm_obj_clone_into(&src, &dst) && child(holder, 0) == first && holder->head.f.upd);
  ck("renamed schema rebuilds the snapshot", !vm_obj_clone_into(&other, &dst) && vm_obj_schema_matches(renamed, child(holder, 0)) && value(child(holder, 0)) == 99);
  ck("rebuild releases the previous snapshot", live_objects() == 1);

  vm_obj_h schema_a = mk(9, VM_OBJ_PTR, 1, NULL, true);
  vm_obj_h schema_b = mk(10, VM_OBJ_PTR, 1, NULL, true);
  vm_obj_link_direct(schema_a, 0, source);
  vm_obj_link_direct(schema_b, 0, renamed);
  vm_accessor_t sa = {.id = 9}, sb = {.id = 10};
  ck("nested storage shapes agree while schemas differ", vm_obj_shape_matches(schema_a, schema_b) && !vm_obj_schema_matches(schema_a, schema_b));
  ck("nested rename replaces field identity", !vm_obj_clone_into(&sa, &dst) && !vm_obj_clone_into(&sb, &dst) && vm_obj_get_child(child(holder, 0), "temp") == NULL && value(vm_obj_get_child(child(holder, 0), "pressure")) == 99);

  // Source is owned solely by the destination tree that Clone replaces.
  a = dynamic(VM_OBJ_PTR, 1);
  leaf = dynamic(VM_OBJ_U32, 1);
  VM_OBJ_SET_VAL_AT((uint32_t)1234, leaf, 0);
  ck("aliased clone fixture linked", !vm_obj_link_direct(a, 0, leaf) && !vm_obj_link_direct(holder, 0, a));
  vm_index_t path[] = {{.kind = VM_IDX_LITERAL, .value = 0}, {.kind = VM_IDX_LITERAL, .value = 0}};
  vm_accessor_t nested = {.id = 2, .count = 2, .indices = path};
  ck("clone reads descendant before releasing its parent", !vm_obj_clone_into(&nested, &dst) && value(child(holder, 0)) == 1234);
  ck("alias replacement retains only the new snapshot", live_objects() == 1);

  vm_obj_h immutable = mk(3, VM_OBJ_U32, 1, "temp", false);
  ck("immutable target fixture linked", !vm_obj_link_direct(holder, 0, immutable));
  holder->head.f.upd = 0;
  ck("same-schema Clone refuses immutable root", vm_obj_clone_into(&src, &dst) != NULL && value(immutable) == 0 && !holder->head.f.upd);

  vm_obj_h src_tree = mk(4, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h dst_tree = mk(5, VM_OBJ_PTR, 2, NULL, true);
  vm_obj_h writable_leaf = mk(6, VM_OBJ_U32, 1, NULL, true);
  vm_obj_h bad_leaf = mk(7, VM_OBJ_U8, 1, NULL, true);
  vm_obj_link_direct(src_tree, 0, source);
  vm_obj_link_direct(src_tree, 1, source);
  vm_obj_link_direct(dst_tree, 0, writable_leaf);
  vm_obj_link_direct(dst_tree, 1, bad_leaf);
  dst_tree->head.f.upd = writable_leaf->head.f.upd = 0;
  vm_accessor_t st = {.id = 4}, dt = {.id = 5};
  ck("late copy mismatch leaves earlier leaves unchanged", vm_obj_copy_content(&st, &dt) != NULL && value(writable_leaf) == 0);
  ck("failed copy publishes no freshness", !dst_tree->head.f.upd && !writable_leaf->head.f.upd);
  vm_obj_link_direct(dst_tree, 1, immutable);
  dst_tree->head.f.upd = 0;
  ck("late immutable leaf also leaves earlier leaves unchanged", vm_obj_copy_content(&st, &dt) != NULL && value(writable_leaf) == 0 && !dst_tree->head.f.upd);

  // Protected producer outputs are still writable internally, and ordinary
  // user variables retain the old wire defaults (protection uses a spare bit).
  vm_obj_h protected = mk(8, VM_OBJ_U32, 1, NULL, true);
  protected->head.f.usr_protected = 1;
  vm_accessor_t guard = {.id = 8};
  vm_accessor_cache_build(&guard);
  ck("internal producer can write protected output", !VM_OBJ_SET_VAL((uint32_t)7, &guard));
  protected->head.f.upd = 0;
  ck("cached user write cannot bypass protection", VM_OBJ_SET_VAL_USR((uint32_t)9, &guard) != NULL && value(protected) == 7 && !protected->head.f.upd);
  guard.flags = 0;
  ck("uncached user write is equally protected", VM_OBJ_SET_VAL_USR((uint32_t)9, &guard) != NULL);
  ck("direct user write is protected", VM_OBJ_SET_VAL_AT_USR((uint32_t)9, protected, 0) != NULL);
  ck("user Copy is protected", vm_obj_copy_content_usr(&src, &guard) != NULL && value(protected) == 7);
  ck("user publish is protected", vm_obj_publish_usr(protected) != NULL && !protected->head.f.upd);
  ck("internal publish is explicit", !vm_obj_publish(protected) && protected->head.f.upd);
  vm_obj_link_direct(dst_tree, 1, protected);
  dst_tree->head.f.upd = 0;
  ck("deep user copy validates all protected leaves first", vm_obj_copy_content_usr(&st, &dt) != NULL && value(writable_leaf) == 0 && !dst_tree->head.f.upd);
  holder->head.f.usr_protected = 1;
  ck("user cannot replace a protected holder", vm_obj_clone_into_usr(&other, &dst) != NULL && child(holder, 0) == immutable);
  ck("user cannot link a protected holder", vm_obj_link_usr(&src, &dst) != NULL);
  holder->head.f.usr_protected = 0;

  vm_obj_link_direct(holder, 0, source);
  holder->head.f.upd = 0;
  ck("field write succeeds without implicit parent publication", !VM_OBJ_SET_VAL_USR((uint32_t)43, &src) && source->head.f.upd && !holder->head.f.upd);
  ck("caller may explicitly publish aggregate", !vm_obj_publish_usr(holder) && holder->head.f.upd);
  vm_index_t named_path[] = {VM_IDX_BY_NAME("temp")};
  vm_accessor_t named_slot = {.id = 2, .count = 1, .indices = named_path};
  vm_obj_h owner = NULL;
  ck("named pointer slot freshness belongs to container", !vm_obj_get_owner(&owner, &named_slot) && owner == holder);

  path[0].value = 0x40000000u;
  uint32_t read = 77;
  ck("overflowing two-step literal is rejected", VM_OBJ_GET_VAL(read, &nested) != NULL && read == 77);
  ck("overflowing write cannot reach child zero", VM_OBJ_SET_VAL((uint32_t)9, &nested) != NULL && value(source) == 43);

  // A failed allocation must not replace the existing graph or publish it.
  holder->head.f.upd = 0;
  for (uint16_t i = live_objects(); i < VM_DYN_MAX - 2; i++) (void)dynamic(VM_OBJ_U8, 1);
  ck("failed partial clone releases its unpublished allocations", vm_obj_clone_into(&st, &dst) != NULL && live_objects() == VM_DYN_MAX - 2);
  ck("partial clone failure preserves destination and freshness", child(holder, 0) == source && !holder->head.f.upd);
  for (uint16_t i = live_objects(); i < VM_DYN_MAX; i++) (void)dynamic(VM_OBJ_U8, 1);
  ck("exhausted Clone keeps the old destination", vm_obj_clone_into(&other, &dst) != NULL && child(holder, 0) == source && !holder->head.f.upd);
  vm_store_reset();
  ck("storage reset frees both allocation domains", live_objects() == 0 && !vm_obj_get_by_id(0));

  ck("program opens for lifecycle test", !vm_loader_open(4, 4, 1, 2048));
  vm_obj_h program_holder = mk(0, VM_OBJ_PTR, 1, NULL, true);
  leaf = dynamic(VM_OBJ_U32, 1);
  vm_obj_link_direct(program_holder, 0, leaf);
  const uint16_t subscribed[] = {0};
  vm_sub_subscribe(subscribed, 1);
  vm_exec_set_mode(VM_RUN_RUNNING);
  vm_obj_head_t h = vm_make_obj_head(VM_OBJ_U32, 1, VM_OBJ_F_MUTABLE, 0);
  ck("upload initialization cannot mutate a running program", vm_loader_add_obj(1, &h, NULL) != NULL);
  ck("failed open retains program and run mode", vm_loader_open(4, 4, 1, 1) != NULL && vm_obj_get_by_id(0) == program_holder && vm_exec_mode() == VM_RUN_RUNNING && vm_sub_count() == 1 && live_objects() == 1);
  ck("successful open owns all teardown", !vm_loader_open(8, 4, 2, 2048) && !vm_obj_get_by_id(0) && live_objects() == 0 && vm_sub_count() == 0 && vm_exec_mode() == VM_RUN_STOPPED);

  vm_obj_h result = mk(0, VM_OBJ_U32, 1, NULL, true);
  vm_obj_h eno = mk(1, VM_OBJ_B, 1, NULL, true);
  uint16_t output_ids[] = {0};
  vm_block_cfg_t cfg = {.q_cnt = 1, .out_obj_ids = output_ids, .eno_obj_id = 1};
  vm_block_h block = NULL;
  ck("block construction protects its published outputs", !vm_block_create(&block, 0, &cfg) && result->head.f.usr_protected && eno->head.f.usr_protected);
  vm_obj_h input = mk(2, VM_OBJ_U32, 1, NULL, true);
  mk(3, VM_OBJ_B, 1, NULL, true);
  vm_obj_h guarded_holder = mk(4, VM_OBJ_PTR, 1, NULL, true);
  guarded_holder->head.f.usr_protected = 1;
  VM_OBJ_SET_VAL_AT((uint32_t)321, input, 0);
  vm_accessor_t *input_acc, *result_acc, *holder_acc;
  vm_accessor_create(&input_acc, 0, 2, 0);
  vm_accessor_create(&result_acc, 1, 0, 0);
  vm_accessor_create(&holder_acc, 2, 4, 0);
  uint16_t set_inputs[] = {0, 1};
  vm_block_cfg_t set_cfg = {.in_cnt = 2, .in_acc_ids = set_inputs, .eno_obj_id = 3};
  vm_block_h writer = NULL;
  ck("user writer block constructed", !vm_block_create(&writer, 1, &set_cfg));
  g_vm_block_fault = false;
  vm_blk_set(writer);
  ck("SET block cannot forge another block's output", g_vm_block_fault && value(result) == 0);
  vm_block_get_inputs(writer)[1] = holder_acc;  // fixture wiring before the next invocation
  g_vm_block_fault = false;
  vm_blk_clone(writer);
  ck("CLONE block cannot replace a protected holder", g_vm_block_fault && child(guarded_holder, 0) == NULL && live_objects() == 0);
  leaf = dynamic(VM_OBJ_U32, 1);
  (void)leaf;
  vm_sub_subscribe(subscribed, 1);
  vm_loader_reset();
  ck("program reset clears objects, subscriptions and mode", live_objects() == 0 && !vm_obj_get_by_id(0) && vm_sub_count() == 0 && vm_loader_state() == VM_LOAD_EMPTY && vm_exec_mode() == VM_RUN_STOPPED);
}
