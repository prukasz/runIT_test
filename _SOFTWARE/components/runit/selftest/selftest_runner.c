#include "selftest_frame.h"
#include "vm_block_expr.h"
#include "vm_selftest.h"

// --- Statistics and Assertion reporting ---
static int s_pass = 0;
static int s_fail = 0;
static char s_failed_checks[16][96];
static int s_failed_count = 0;

void selftest_ck(const char* what, bool ok) {
  if (ok) {
    s_pass++;
    ESP_LOGI(TAG, "  PASS  %s", what);
  } else {
    s_fail++;
    if (s_failed_count < 16) {
      strncpy(s_failed_checks[s_failed_count], what ? what : "unknown", 95);
      s_failed_checks[s_failed_count][95] = '\0';
      s_failed_count++;
    }
    ESP_LOGE(TAG, "  FAIL  %s", what);
  }
}

int selftest_get_pass(void) {
  return s_pass;
}

int selftest_get_fail(void) {
  return s_fail;
}

void selftest_reset_counts(void) {
  s_pass = 0;
  s_fail = 0;
}

// --- Common Arena & Object Helpers ---
void direct_arena_reset(void) {
  const uint16_t counts[VM_REG_CNT] = {[VM_REG_OBJ] = 32, [VM_REG_ACC] = 24, [VM_REG_BLK] = 8};
  (void)vm_store_open(DIRECT_POOL, counts);
}

vm_obj_head_t hd(vm_obj_t_e type, uint16_t items) {
  vm_obj_head_t h = {0};
  h.payload_size = (uint16_t)(items * vm_type_width(type));
  h.d.obj_t = (uint8_t)type;
  return h;
}

vm_obj_h mk(uint16_t id, vm_obj_t_e type, uint16_t items, const char* name, bool mutable_) {
  vm_obj_head_t h = hd(type, items);
  h.d.name_size = name ? (uint8_t)strlen(name) : 0;
  h.f.mutable = mutable_ ? 1 : 0;
  vm_obj_h o = NULL;
  if (vm_obj_create(&o, id, &h, name) != NULL) {
    return NULL;
  }
  return o;
}

uint32_t kf(float f) {
  vm_expr_k_t k = {.f = f};
  return k.u;
}

bool near_f(float a, float b) {
  return fabsf(a - b) < 1e-4f;
}

// --- Frame Builder Helpers ---
static uint8_t s_frame[256];
static size_t s_len;

void f_begin(uint8_t class_header, uint8_t packet) {
  s_len = 0;
  s_frame[s_len++] = class_header;
  s_frame[s_len++] = packet;
}

void f_u8(uint8_t v) {
  s_frame[s_len++] = v;
}

void f_u16(uint16_t v) {
  s_frame[s_len++] = (uint8_t)(v & 0xFF);
  s_frame[s_len++] = (uint8_t)(v >> 8);
}

void f_u32(uint32_t v) {
  f_u16((uint16_t)(v & 0xFFFF));
  f_u16((uint16_t)(v >> 16));
}

void f_blob(const void* p, size_t n) {
  memcpy(s_frame + s_len, p, n);
  s_len += n;
}

void f_str(const char* s) {
  f_blob(s, strlen(s));
}

void f_f32(float v) {
  f_blob(&v, sizeof(v));
}

err_h f_send(void) {
  return sys_interface_decode(s_frame, s_len);
}

err_h upload_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint32_t total) {
  f_begin(VM_LOADER_CLASS_HEADER, 0x41);
  f_u16(obj_cnt);
  f_u16(acc_cnt);
  f_u16(blk_cnt);
  f_u32(total);
  return f_send();
}

void add_obj_record_raw(uint16_t id, uint16_t payload_size, uint8_t type, uint8_t flags, const char* name) {
  f_u16(id);
  vm_obj_head_t head = {0};
  head.payload_size = payload_size;
  head.d.obj_t = type & 0x0F;
  head.d.name_size = name ? (uint8_t)strlen(name) : 0;
  head.f.mutable = (flags & VM_OBJ_F_MUTABLE) != 0;
  head.f.upd_resetable = (flags & VM_OBJ_F_UPD_RESETABLE) != 0;
  head.f.retentive = (flags & VM_OBJ_F_RETENTIVE) != 0;
  head.f.usr_protected = (flags & VM_OBJ_F_USR_PROTECTED) != 0;
  f_blob(&head, sizeof(head));
  if (name && head.d.name_size) f_str(name);
}

void add_obj_record(uint16_t id, uint16_t item_count, uint8_t type, uint8_t flags, const char* name) {
  add_obj_record_raw(id, (uint16_t)(item_count * vm_type_width((vm_obj_t_e)type)), type, flags, name);
}

uint8_t s_idx[64];
uint8_t s_idx_len;

void ix_begin(void) {
  s_idx_len = 0;
}

void ix_literal(uint32_t v) {
  s_idx[s_idx_len++] = VM_IDX_LITERAL;
  s_idx[s_idx_len++] = (uint8_t)(v & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 8) & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 16) & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)((v >> 24) & 0xFF);
}

void ix_ref(uint16_t acc_id) {
  s_idx[s_idx_len++] = VM_IDX_REF;
  s_idx[s_idx_len++] = (uint8_t)(acc_id & 0xFF);
  s_idx[s_idx_len++] = (uint8_t)(acc_id >> 8);
}

void ix_name(const char* n) {
  uint8_t l = (uint8_t)strlen(n);
  s_idx[s_idx_len++] = VM_IDX_NAME;
  s_idx[s_idx_len++] = l;
  memcpy(s_idx + s_idx_len, n, l);
  s_idx_len += l;
}

void acc_record(uint16_t acc_id, uint16_t root_id, uint8_t idx_count) {
  f_u16(acc_id);
  f_u16(root_id);
  f_u8(idx_count);
  f_u8(s_idx_len);
  f_blob(s_idx, s_idx_len);
}

// --- Forward Declarations of Stage Functions ---
#if RUNIT_TEST_SECTION_OBJ
// Group 1: Object model & Accessors
void test_header_helpers(void);
void test_conversion(void);
void test_resolution(void);
void test_nested(void);
void test_mutation(void);
void test_block_api(void);
void test_obj_construction(void);
void test_names_and_accessor_build(void);
void test_access_edges(void);
void test_strings(void);
void test_resolution_cache(void);
void test_object_contracts(void);
#endif

#if RUNIT_TEST_SECTION_LOADER
// Group 2: Loader & Wire Protocol
void test_upload(void);
void test_malformed(void);
void test_block_upload(void);
void test_dynamic_objects(void);
void test_palette(void);
#endif

#if RUNIT_TEST_SECTION_EXEC
// Group 3: Execution Runtime & Control Blocks
void test_exec_pass(void);
void test_events(void);
void test_expr(void);
void test_branch(void);
void test_for(void);
void test_set(void);
void test_clone(void);
void test_json_pipeline(void);
void test_step_selection_pipeline(void);
void test_math_pi(void);
void test_math_primes(void);
void test_runtime_override(void);
#endif

#if RUNIT_TEST_SECTION_SUB
// Group 4: Subscription & Telemetry
void test_subscription(void);
#endif

// --- Extensible Stage Registry ---
static const selftest_stage_t s_stages[] = {
#if RUNIT_TEST_SECTION_OBJ
    // Group 1: Object model & Accessors
    {"A", "header helpers / type tables", test_header_helpers, true},
    {"B", "value conversion", test_conversion, true},
    {"C", "accessor resolution", test_resolution, true},
    {"D", "nested objects", test_nested, true},
    {"E", "mutation & copy", test_mutation, true},
    {"F", "block API", test_block_api, true},
    {"G", "object construction", test_obj_construction, true},
    {"H", "names and accessor build", test_names_and_accessor_build, true},
    {"K", "resolution cache", test_resolution_cache, true},
    {"I", "access edges", test_access_edges, true},
    {"J", "string objects", test_strings, true},
    {"OBJ", "ownership, schema and mutation contracts", test_object_contracts, true},
#endif

#if RUNIT_TEST_SECTION_LOADER
    // Group 2: Loader & Wire Protocol
    {"L", "upload protocol", test_upload, true},
    {"N", "block upload", test_block_upload, true},
    {"M", "malformed packets", test_malformed, true},
    {"O", "dynamic objects", test_dynamic_objects, true},
    {"P", "palette resolution", test_palette, true},
#endif

#if RUNIT_TEST_SECTION_EXEC
    // Group 3: Execution Runtime & Control Blocks
    {"R", "the pass", test_exec_pass, true},
    {"S", "events", test_events, true},
    {"T", "expression block", test_expr, true},
    {"U", "branch block (flow routers)", test_branch, true},
    {"V", "for loop block", test_for, true},
    {"W", "set block", test_set, true},
    {"X", "the Clone block", test_clone, true},
    {"Y", "clone, compute, write back", test_json_pipeline, true},
    {"PIPE", "selection pipeline step-by-step & telemetry", test_step_selection_pipeline, true},
    {"PI", "Nilakantha pi series calculation", test_math_pi, true},
    {"PRIME", "prime tester (trial division in loop)", test_math_primes, true},
    {"OVERRIDE", "runtime variable update between scans", test_runtime_override, true},
#endif

#if RUNIT_TEST_SECTION_SUB
    // Group 4: Subscription & Telemetry
    {"SUB", "subscription & telemetry", test_subscription, true},
#endif
};

// --- Test Runner Entry Point ---
void vm_selftest_run(void) {
  selftest_reset_counts();
  s_failed_count = 0;
  size_t total_stages = sizeof(s_stages) / sizeof(s_stages[0]);
  int stage_failures[32] = {0};
  ESP_LOGW(TAG, "==== VM self-test start (%u stages registered) ====", (unsigned)total_stages);

  for (size_t i = 0; i < total_stages; i++) {
    const selftest_stage_t* st = &s_stages[i];
    if (!st->enabled || !st->run) {
      ESP_LOGW(TAG, "  SKIP  [%s] %s", st->id, st->name);
      continue;
    }

    int p_before = selftest_get_pass();
    int f_before = selftest_get_fail();

    st->run();
    vTaskDelay(1);

    int stage_pass = selftest_get_pass() - p_before;
    int stage_fail = selftest_get_fail() - f_before;
    if (i < 32) stage_failures[i] = stage_fail;

    if (stage_fail > 0) {
      ESP_LOGE(TAG, "  STAGE [%s] %s: %d passed, %d FAILED", st->id, st->name, stage_pass, stage_fail);
    } else {
      ESP_LOGI(TAG, "  STAGE [%s] %s: %d passed, 0 failed", st->id, st->name, stage_pass);
    }
  }

  vm_loader_reset();

  int total_pass = selftest_get_pass();
  int total_fail = selftest_get_fail();
  if (total_fail == 0) {
    ESP_LOGW(TAG, "==== VM self-test: %d passed, 0 failed (scratch arena used %lu/%u B) ====", total_pass, (unsigned long)vm_store_used(),
             (unsigned)vm_store_capacity());
  } else {
    ESP_LOGE(TAG, "==== VM self-test: %d passed, %d FAILED ====", total_pass, total_fail);
    for (size_t i = 0; i < total_stages && i < 32; i++) {
      if (stage_failures[i] > 0) {
        ESP_LOGE(TAG, "  [FAILED STAGE] [%s] %s (%d checks failed)", s_stages[i].id, s_stages[i].name, stage_failures[i]);
      }
    }
    for (int i = 0; i < s_failed_count; i++) {
      ESP_LOGE(TAG, "  [FAILED CHECK %d] %s", i + 1, s_failed_checks[i]);
    }
  }
}

err_h vm_selftest_inject(const uint8_t* frame, size_t len) {
  return sys_interface_decode(frame, len);
}
