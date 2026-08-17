#include "vm_section.h"

#define OWNER OWNER_VM_EXEC

err_h vm_section_create(uint16_t sec_id, uint16_t start, uint16_t end) {
  uint16_t blk_cnt = g_vm_store.reg[VM_REG_BLK].count;

  /* Empty is rejected along with out-of-range. An empty section is not
     harmless: it is a freeze point with nothing behind it, so a program full
     of them would look like it had sections while behaving as one long
     uninterruptible pass. */
  if (end <= start || end > blk_cnt) {
    SE_RET_ERR(ERR_VM_SEC_BAD_RANGE, .sec_id = sec_id, .start = start, .end = end, .blk_cnt = blk_cnt);
  }

  /* Linear against what is already bound. Sections are a handful per program
     -- tens at the very most -- and this runs once per section at load, so the
     quadratic term never leaves the noise. */
  uint16_t n = vm_section_count();
  for (uint16_t i = 0; i < n; i++) {
    const vm_section_t* s = vm_section_by_id(i);
    if (!s) continue;
    if (start < s->end && s->start < end) {
      SE_RET_ERR(ERR_VM_SEC_OVERLAP, .sec_id = sec_id, .other_id = i, .start = start, .end = end);
    }
  }

  vm_section_t* sec = NULL;
  // allocates, zeroes and binds the id in one step -- see vm_store.h
  SE_RET_IF_ERR(vm_store_alloc((void**)&sec, VM_REG_SEC, sec_id, (uint32_t)sizeof(*sec)));

  sec->start = start;
  sec->end = end;
  return NULL;
}
