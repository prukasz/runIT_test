#include "vm_loader.h"
#include <string.h>
#include "vm_exec.h"
#include "vm_sub.h"

#define OWNER OWNER_VM_LOADER

static vm_load_state_e s_state = VM_LOAD_EMPTY;

static err_h require_state(vm_load_state_e want) {
  if (s_state != want) {
    SE_RET_ERR(ERR_VM_LOAD_BAD_STATE, .state = (uint8_t)s_state, .expected = (uint8_t)want);
  }
  if (vm_exec_mode() != VM_RUN_STOPPED) {
    SE_RET_ERR(ERR_VM_LOAD_RUNNING, .mode = (uint8_t)vm_exec_mode());
  }
  return NULL;
}

void vm_loader_reset(void) {
  (void)vm_exec_program_lock();
  vm_exec_reset();
  vm_sub_reset();
  vm_store_reset();
  s_state = VM_LOAD_EMPTY;
  vm_exec_program_unlock(VM_RUN_STOPPED);
}

err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint16_t sec_cnt, uint32_t total_size) {
  vm_run_mode_e previous = vm_exec_program_lock();

  const uint16_t counts[VM_REG_CNT] = {
      [VM_REG_OBJ] = obj_cnt,
      [VM_REG_ACC] = acc_cnt,
      [VM_REG_BLK] = blk_cnt,
      [VM_REG_SEC] = sec_cnt,
  };
  err_h e = vm_store_open(total_size, counts);
  if (e) {
    vm_exec_program_unlock(previous);
    return e;
  }

  vm_exec_reset();
  vm_sub_reset();
  s_state = VM_LOAD_OPEN;
  vm_exec_program_unlock(VM_RUN_STOPPED);
  return NULL;
}

err_h vm_loader_add_obj(uint16_t id, const vm_obj_head_t* head, const char* name) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));
  SE_CHECK_NOT_NULL(head);

  /* Checked before the 4-bit obj_t field is used: an out-of-range type
     would otherwise index the width table out of bounds. */
  if (!vm_type_ok(head->d.obj_t)) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_TYPE, .type = head->d.obj_t);
  }

  vm_obj_h obj = NULL;
  SE_RET_IF_ERR(vm_obj_create(&obj, id, head, head->d.name_size ? name : NULL));
  return NULL;
}

err_h vm_loader_set_data(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));
  SE_CHECK_NOT_NULL(data);

  vm_obj_h obj = vm_obj_by_id(id);
  if (!obj) {
    SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
  }

  uint16_t items = vm_obj_items_cnt(obj);

  if ((vm_obj_t_e)obj->head.d.obj_t == VM_OBJ_PTR) {
    /* Children arrive as ids, never as addresses -- a pointer is meaningless
       outside this boot's arena. Two bytes each, little-endian. */
    if ((len & 1u) != 0) {
      SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
    }
    uint16_t n = len / 2u;
    if ((uint32_t)start_idx + n > items) {
      SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
    }
    for (uint16_t i = 0; i < n; i++) {
      uint16_t child_id = (uint16_t)(data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8));
      vm_obj_h child = vm_obj_by_id(child_id);
      if (!child) {
        // forward reference, or an id the program never created
        SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = child_id);
      }
      SE_RET_IF_ERR(vm_obj_link_direct(obj, (uint16_t)(start_idx + i), child));
    }
    return NULL;
  }

  uint8_t w = vm_obj_type_size(obj);
  if (w == 0 || (len % w) != 0) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
  }
  uint16_t n = (uint16_t)(len / w);
  if ((uint32_t)start_idx + n > items) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
  }

  memcpy(obj->payload + (size_t)start_idx * w, data, len);
  return NULL;
}

err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));

  vm_accessor_t* acc = NULL;
  SE_RET_IF_ERR(vm_accessor_create(&acc, acc_id, root_obj_id, idx_count));

  size_t off = 0;
  for (uint8_t i = 0; i < idx_count; i++) {
    if (off + 1 > idx_len) {
      SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 1, .got = (uint16_t)(idx_len - off));
    }
    uint8_t kind = idx_data[off++];

    switch (kind) {
      case VM_IDX_LITERAL: {
        if (off + 4 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 4, .got = (uint16_t)(idx_len - off));
        }
        uint32_t v = (uint32_t)idx_data[off] | ((uint32_t)idx_data[off + 1] << 8) |
                     ((uint32_t)idx_data[off + 2] << 16) | ((uint32_t)idx_data[off + 3] << 24);
        off += 4;
        SE_RET_IF_ERR(vm_accessor_set_literal(acc, i, v));
        break;
      }
      case VM_IDX_REF: {
        if (off + 2 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 2, .got = (uint16_t)(idx_len - off));
        }
        uint16_t ref_id = (uint16_t)(idx_data[off] | ((uint16_t)idx_data[off + 1] << 8));
        off += 2;
        /* Must already be built. Requiring that is what makes a reference
           cycle unconstructable rather than merely depth-capped later. */
        vm_accessor_t* ref = vm_accessor_by_id(ref_id);
        if (!ref) {
          SE_RET_ERR(ERR_VM_REG_OOB, .kind = VM_REG_ACC, .id = ref_id, .count = g_vm_store.reg[VM_REG_ACC].count);
        }
        SE_RET_IF_ERR(vm_accessor_set_ref(acc, i, ref));
        break;
      }
      case VM_IDX_NAME: {
        if (off + 1 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 1, .got = (uint16_t)(idx_len - off));
        }
        uint8_t nlen = idx_data[off++];
        if (off + nlen > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = nlen, .got = (uint16_t)(idx_len - off));
        }
        SE_RET_IF_ERR(vm_accessor_set_name(acc, i, (const char*)(idx_data + off), nlen));
        off += nlen;
        break;
      }
      default:
        SE_RET_ERR(ERR_VM_ACC_BAD_KIND, .acc_id = acc_id, .pos = i, .kind = kind);
    }
  }

  /* Pre-resolve now the indices are set. Objects arrive before accessors, so
     the root is normally already there; if it is not, this returns false and
     the accessor just resolves the long way. Nothing to check. */
  (void)vm_accessor_cache_build(acc);
  return NULL;
}

err_h vm_loader_add_block(uint16_t blk_id, const vm_block_cfg_t* cfg) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));
  SE_CHECK_NOT_NULL(cfg);

  /* Against the block table before anything is built. `block_type` used to be
     carried and stored with nothing reading it, which meant a program could
     name a type nothing knows how to run and find out by being silently
     skipped on every pass forever. Checked first for the same reason the id
     is: a rejected block should cost the arena nothing, so the retry still
     fits.

     This is the only shape check there is. What a block *takes in* -- pin
     counts, private-state size, types -- is not described anywhere yet, so a
     program that declares the wrong custom_len for its type is still its own
     problem. That table comes with the palette. */
  SE_RET_IF_ERR(vm_exec_check_block_type(blk_id, cfg->block_type));

  vm_block_h blk = NULL;
  SE_RET_IF_ERR(vm_block_create(&blk, blk_id, cfg));
  return NULL;
}

err_h vm_loader_add_section(uint16_t sec_id, uint16_t start, uint16_t end) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));
  SE_RET_IF_ERR(vm_section_create(sec_id, start, end));
  return NULL;
}

vm_load_state_e vm_loader_state(void) {
  return s_state;
}

uint32_t vm_loader_used(void) {
  return vm_store_used();
}
