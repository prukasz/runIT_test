#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block.h"  // vm_span_t
#include "vm_store.h"

/*
Sections are atomic units, not schedules.

The device never sorts. The client walks the wire graph, topologically sorts
each connected component and uploads its blocks in that order, so **the block
registry index is the execution order** and the supervisor simply walks ids
0..n-1. The whole forest -- standalone blocks and long chains alike -- merges
into one top-to-bottom order, and there is no per-section ordering because
there is only one order.

So a section says nothing about *whether* or *when* its blocks run. Every
section runs every pass. What it says is that once started it finishes before
anything may intervene: a section is the only place the system may be
interrupted, and freeze, resume, event delivery, remote overrides and the
double-buffered swap all take effect at a section boundary. Never mid-section,
where half the blocks would have acted on new inputs and half on old.

That is also why the granularity is worth having at all: freeze latency is
bounded by the longest *section* rather than by a whole pass over 300 blocks.

A section is therefore a `[start, end)` range over the one global order --
exactly the shape a block claiming a sub-range of its own uses, and reusing
vm_span_t says so.

It also settles label ordering, without any mechanism: a label read *below* its
writer sees this pass's value, *above* it, last pass's. Not a race -- a
position, readable off the canvas.
*/

typedef vm_span_t vm_section_t;

/** @brief id -> section, NULL past the end of the loaded program. Fourth of
 *  the registries in vm_store.h; same rules as objects, accessors and blocks. */
static inline const vm_section_t* vm_section_by_id(uint16_t id) {
  return (const vm_section_t*)vm_store_get(VM_REG_SEC, id);
}

/** @brief How many sections the loaded program declared. */
static inline uint16_t vm_section_count(void) {
  return g_vm_store.reg[VM_REG_SEC].count;
}

/**
 * @brief Create one section and bind it to @p sec_id.
 *
 * Validated against the block registry, which has to exist first -- the same
 * ordering rule a block has with the accessors it names, one level up.
 *
 * Overlap is rejected, not tolerated. Two sections claiming the same block
 * would run it twice in one pass, which for an actuator means two commands and
 * for a counter means double counting; and because the ranges come off the
 * wire, an overlap is far more likely to be a client bug than an intent.
 *
 * @param start First block id in the range.
 * @param end One past the last.
 * @return err_h ERR_VM_SEC_BAD_RANGE for an empty or out-of-bounds range,
 *         ERR_VM_SEC_OVERLAP naming the section it collides with,
 *         ERR_VM_REG_OOB / ERR_VM_REG_DUP for the id, or ERR_BASE_NO_MEM.
 */
err_h vm_section_create(uint16_t sec_id, uint16_t start, uint16_t end);
