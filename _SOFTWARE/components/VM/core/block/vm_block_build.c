#include "vm_block_build.h"
#include <string.h>
#include "esp_compiler.h"

#define OWNER OWNER_VM_BLOCK

// which wiring point an ERR_VM_BLK_BAD_REF refers to
#define REF_KIND_IN 0
#define REF_KIND_OUT 1
#define REF_KIND_EN 2
#define REF_KIND_ENO 3

err_h vm_block_create(vm_block_h* out, uint16_t id, const vm_block_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(cfg);
  *out = NULL;

  if (cfg->in_cnt > VM_BLOCK_MAX_IN || cfg->q_cnt > VM_BLOCK_MAX_OUT || cfg->en_cnt > VM_BLOCK_MAX_EN) {
    SE_RET_ERR(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }
  if ((cfg->in_cnt && !cfg->in_acc_ids) || (cfg->q_cnt && !cfg->out_obj_ids) || (cfg->en_cnt && !cfg->en_acc_ids)) {
    SE_RET_ERR(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }

  /* Everything is resolved before anything is allocated. A block that names a
     missing accessor should cost the arena nothing -- otherwise a rejected
     program still shrinks the space the retry has to fit in. */
  /* NO_ID on an input means the pin is deliberately unwired -- the block uses
     its own constant. Any other id must resolve. */
  for (uint8_t i = 0; i < cfg->in_cnt; i++) {
    if (cfg->in_acc_ids[i] == VM_BLOCK_NO_ID) continue;
    if (!vm_accessor_by_id(cfg->in_acc_ids[i])) {
      SE_RET_ERR(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->in_acc_ids[i], .slot = i, .kind = REF_KIND_IN);
    }
  }
  for (uint8_t i = 0; i < cfg->q_cnt; i++) {
    if (!vm_obj_by_id(cfg->out_obj_ids[i])) {
      SE_RET_ERR(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->out_obj_ids[i], .slot = i, .kind = REF_KIND_OUT);
    }
  }

  /* Every enable source must resolve. Absence is spelled by en_cnt == 0, not
     by a NO_ID entry: a listed source that does not resolve is a malformed
     program, exactly like an unwired numbered pin. */
  for (uint8_t i = 0; i < cfg->en_cnt; i++) {
    if (!vm_accessor_by_id(cfg->en_acc_ids[i])) {
      SE_RET_ERR(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->en_acc_ids[i], .slot = i, .kind = REF_KIND_EN);
    }
  }

  /* ENO stays optional, and is the one place NO_ID is still meaningful. */
  vm_obj_h eno = NULL;
  if (cfg->eno_obj_id != VM_BLOCK_NO_ID) {
    eno = vm_obj_by_id(cfg->eno_obj_id);
    if (!eno) {
      SE_RET_ERR(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->eno_obj_id, .slot = 0, .kind = REF_KIND_ENO);
    }
  }

  size_t total = vm_block_size(cfg->in_cnt, cfg->q_cnt, cfg->en_cnt, cfg->custom_len);
  vm_block_h b = NULL;
  // allocates, zeroes and binds the id in one step -- see vm_store.h
  SE_RET_IF_ERR(vm_store_alloc((void**)&b, VM_REG_BLK, id, (uint32_t)total));

  b->cfg.block_idx = cfg->block_idx;
  b->cfg.block_type = cfg->block_type;
  b->cfg.in_cnt = cfg->in_cnt;
  b->cfg.q_cnt = cfg->q_cnt;
  b->cfg.en_cnt = cfg->en_cnt;
  b->cfg.en_mode = cfg->en_mode;
  b->cfg.on_error = cfg->on_error;
  b->cfg.custom_len = cfg->custom_len;
  b->cfg.eno = eno;

  /* Pointers, not ids, from here on -- resolving once at load is what removes
     the invalid-id failure mode from every later access. */
  const vm_accessor_t** ins = vm_block_inputs(b);
  for (uint8_t i = 0; i < cfg->in_cnt; i++) {
    // NO_ID stays NULL: the pin exists, nothing is wired to it
    ins[i] = (cfg->in_acc_ids[i] == VM_BLOCK_NO_ID) ? NULL : vm_accessor_by_id(cfg->in_acc_ids[i]);
  }

  vm_obj_h* outs = vm_block_outputs(b);
  for (uint8_t i = 0; i < cfg->q_cnt; i++) {
    outs[i] = vm_obj_by_id(cfg->out_obj_ids[i]);
    outs[i]->head.f.usr_protected = 1;
  }
  if (eno) eno->head.f.usr_protected = 1;

  const vm_accessor_t** ens = vm_block_en_list(b);
  for (uint8_t i = 0; i < cfg->en_cnt; i++) ens[i] = vm_accessor_by_id(cfg->en_acc_ids[i]);

  *out = b;
  return NULL;
}

void vm_block_report_error(err_h cause, uint16_t block_idx, uint8_t block_type) {
  /* Same mark BLOCK_CALL leaves -- every route a body reports a failure by has
     to set it, or cfg.on_error would be honoured for some failures and not
     others depending on which macro the block author reached for. */
  g_vm_block_fault = true;
  SE_push_to_handler(SE_WRAP_ERR(cause, ERR_VM_BLOCK_FAILED, .block_idx = block_idx, .block_type = block_type));
}
