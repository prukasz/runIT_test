#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vm_obj_access.h"

/*
One block = one linear-allocator blob, sized once at creation:
  [cfg][in_cnt * const vm_accessor_t*][q_cnt * vm_obj_h][en_cnt * const vm_accessor_t*][custom_data bytes]

No separate allocations for inputs/outputs/enables/custom_data, and nothing to
keep in sync -- their positions are derived from in_cnt/q_cnt/en_cnt, not stored.

Inputs and outputs are deliberately different shapes. An input can be wired
to anything -- any object, any element, possibly through a switch cell, with
a possibly dynamic index -- so it needs the full accessor. An output is bound
once at load time to an object this block owns, and never moves, so there is
nothing to resolve: the slot holds the vm_obj_h itself. That is both smaller
(no chain descriptor) and free at runtime (no walk at all).

An input slot may be **NULL**, meaning the pin exists but nothing is wired to
it -- the block falls back to a constant it keeps in custom_data (a servo with
a typed-in angle rather than a wired one). NULL is the only spelling for that:
there is deliberately no parallel "connected" bitmask, because two encodings of
the same fact can disagree, and `vm_block_get_in()` already reports
ERR_VM_BLOCK_PIN_UNLINKED off the NULL.
*/

/* EN/ENO sit outside the numbered pins so the supervisor can gate every block
   identically without knowing which pin index a given block type happens to
   use. They are ordinary boolean objects otherwise.

   EN is a *list*, because the flow graph is a DAG: a block reached by two
   branches has two enable sources (see [[VM_EXEC.MD]]). The list lives in the
   block's own trailing bytes like the pins do, so a block with the common
   single enable pays one pointer for it and a root block pays nothing at all.

   There is deliberately no `section_idx` here. A block does not know which
   section owns it, for the same reason an object does not know its own id: the
   section's ordered list is the one place that fact lives, and the supervisor
   is walking it, so a copy in the block could only ever disagree with it.
   `block_idx` does stay, because BLOCK_CALL runs *inside* block bodies where
   there is no supervisor frame to ask -- and an error path that depends on
   ambient state being correctly maintained misattributes exactly when
   something is already going wrong. */

/** @brief How a block combines its enable sources. */
#define VM_BLK_EN_ANY 0u  // enabled if *any* source is -- branches rejoining
#define VM_BLK_EN_ALL 1u  // enabled only if *every* source is -- independent conditions

/** @brief What a failing block body does to the flow below it. */
#define VM_BLK_ERR_STOP 0u      // publish a false ENO -- everything downstream self-skips
#define VM_BLK_ERR_CONTINUE 1u  // report and carry on; the flow below is unaffected

typedef struct vm_block_data_t {
  struct {
    uint16_t block_idx;
    uint8_t block_type;
    uint8_t in_cnt;
    uint8_t q_cnt;
    uint8_t en_cnt;    // 0 = root, always enabled (the IEC 61131-3 default)
    uint8_t en_mode;   // VM_BLK_EN_ANY / _ALL; meaningless when en_cnt < 2
    uint8_t on_error;  // VM_BLK_ERR_STOP / _CONTINUE
    /* Length of custom_data. The four section *positions* are still derived
       from in_cnt/q_cnt/en_cnt as above -- this is the one thing that cannot
       be, because nothing downstream of the pins says where the block ends.
       Without it the loader cannot check an uploaded custom_len against what
       the block type expects (a mismatch writes into the next block), and
       nothing generic -- telemetry, a debugger, a block walker -- can find
       the next block in the arena. */
    uint16_t custom_len;
    /* Two bytes of padding sit here: `eno` needs 4-byte alignment and the
       fields above stop at 10. Spend them before growing the header. */
    vm_obj_h eno;  // NULL = block publishes no ENO
  } cfg;
  uint8_t data[];
} vm_block_data_t;

_Static_assert(sizeof(struct vm_block_data_t) == 16, "custom_len must stay inside the alignment padding before `eno`");

typedef vm_block_data_t* vm_block_h;

/* Largest in_cnt a block can declare. Same staging argument as outputs below:
   the decoder holds the ids on its stack while resolving them, and in_cnt's own
   width would make that a 510-byte buffer inside a BLE callback. */
#define VM_BLOCK_MAX_IN 16

/* Outputs have no mask forcing a limit, but the decoder has to stage their ids
   somewhere while it resolves them, and q_cnt's own width would make that a
   510-byte stack buffer inside a BLE callback. 16 is well past any real block
   shape and keeps the staging cost at 32 bytes. */
#define VM_BLOCK_MAX_OUT 16

/* Enable sources per block. Same staging argument as outputs, and well past
   any real shape: a merge of more than 16 branches is a graph the editor
   should have folded into an Expression block long before it got here. */
#define VM_BLOCK_MAX_EN 16

/** @brief id -> block, NULL past the end of the loaded program. Third of the
 *  three registries in vm_store.h; same rules as objects and accessors. */
static inline vm_block_h vm_block_by_id(uint16_t id) {
  return (vm_block_h)vm_store_get(VM_REG_BLK, id);
}

static inline const vm_accessor_t** vm_block_inputs(vm_block_h b) {
  return (const vm_accessor_t**)b->data;
}

static inline vm_obj_h* vm_block_outputs(vm_block_h b) {
  return (vm_obj_h*)(vm_block_inputs(b) + b->cfg.in_cnt);
}

/** @brief This block's enable sources -- `en_cnt` entries, empty for a root. */
static inline const vm_accessor_t** vm_block_en_list(vm_block_h b) {
  return (const vm_accessor_t**)(vm_block_outputs(b) + b->cfg.q_cnt);
}

static inline void* vm_block_custom_data(vm_block_h b) {
  return (void*)(vm_block_en_list(b) + b->cfg.en_cnt);
}

// total bytes required for one block of this shape -- what the loader uses
// to size a single linear-allocator slot before writing into it
static inline size_t vm_block_size(uint8_t in_cnt, uint8_t q_cnt, uint8_t en_cnt, uint16_t custom_len) {
  return sizeof(vm_block_data_t) + (size_t)in_cnt * sizeof(const vm_accessor_t*) + (size_t)q_cnt * sizeof(vm_obj_h) + (size_t)en_cnt * sizeof(const vm_accessor_t*) + custom_len;
}

/** @brief Bytes of custom data this block owns. 0 when it keeps no state. */
static inline uint16_t vm_block_custom_len(vm_block_h b) {
  return b->cfg.custom_len;
}

/**
 * @brief Bytes this block instance actually occupies -- the runtime
 *        counterpart of vm_block_size(), which needs the shape up front.
 *
 * Lets anything generic step from one block to the next in the arena without
 * a separate table of offsets: a supervisor iterating the program, a
 * telemetry dump, a debugger listing.
 */
static inline size_t vm_block_total_size(vm_block_h b) {
  return vm_block_size(b->cfg.in_cnt, b->cfg.q_cnt, b->cfg.en_cnt, b->cfg.custom_len);
}

// Out-of-line error helpers (vm_block_build.c)
err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out);
err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out);
void vm_block_report_error(err_h cause, uint16_t block_idx, uint8_t block_type);

/**
 * @brief Fetch input pin `id`'s accessor.
 * @return err_h NULL on success, ERR_VM_BLOCK_PIN_MISSING if the block has no
 *         such pin, ERR_VM_BLOCK_PIN_UNLINKED if the pin exists but nothing
 *         is wired to it.
 */
static inline err_h vm_block_get_in(const vm_accessor_t** target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.in_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, false);
  const vm_accessor_t* acc = vm_block_inputs(b)[id];
  if (unlikely(!acc)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, false);
  *target = acc;
  return NULL;
}

/** @brief Fetch output pin `id`'s object -- already bound, nothing to resolve.
 *  Same errors as vm_block_get_in(). */
static inline err_h vm_block_get_out(vm_obj_h* target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.q_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, true);
  vm_obj_h obj = vm_block_outputs(b)[id];
  if (unlikely(!obj)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, true);
  *target = obj;
  return NULL;
}

/*
The supervisor calls every block in a section every time that section runs,
and never interprets EN itself -- the block decides what being disabled means
for it. That is deliberate on both counts: if the supervisor skipped disabled
blocks, an actuator could never define a safe fallback ("hold whatever was
last commanded" is usually the wrong failure mode), and a block whose *active*
state is EN false -- an off-delay timer, a falling-edge detector -- could
never run at all.

Scheduling granularity is therefore the section, not the block: an idle event
section costs nothing, but every block inside a section that runs is called.

  IF_BLOCK_ENABLED(block) {
    ... normal work ...
    vm_block_set_ENO(block, true);
  } else {
    servo_go_home(...);            // defined safe state, not "whatever was last"
    vm_block_set_ENO(block, false);
  }

Enable combines the block's sources per `en_mode`: ANY for branches rejoining
("either path reached me"), ALL for independent conditions that must all hold.
No sources at all means a root, always enabled, so a block that never gates
costs one compare.

Absence and unresolvability are opposites on purpose. An *absent* gate is
"nothing is gating me" and reads as enabled; a gate that exists but cannot be
evaluated reads as *disabled*, because running the body when the gate is broken
is the more dangerous guess. A failing source is reported and then counts as
false, which under ANY means one broken branch cannot mask a working one, and
under ALL means a broken condition correctly stops the block.

Deliberately stateless: it reports the level and nothing else. Edge detection
and first-run initialisation are block-type concerns, so they live in that
block's own custom_data struct, alongside whatever else it remembers between
runs -- a block that wants an edge is stateful by definition and already has
somewhere to put it.
*/
static inline bool vm_block_is_enabled(vm_block_h b) {
  uint8_t n = b->cfg.en_cnt;
  if (n == 0) return true;

  const vm_accessor_t** en = vm_block_en_list(b);
  const bool all = (b->cfg.en_mode == VM_BLK_EN_ALL);
  for (uint8_t i = 0; i < n; i++) {
    bool v = false;
    err_h e = VM_OBJ_GET_VAL(v, en[i]);
    if (unlikely(e)) {
      vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
      v = false;  // fail closed, then let the mode decide what that means
    }
    if (all) {
      if (!v) return false;  // ALL: the first false settles it
    } else if (v) {
      return true;  // ANY: the first true settles it
    }
  }
  /* Fell through: under ALL nothing was false, under ANY nothing was true. */
  return all;
}

#define IF_BLOCK_ENABLED(block) if (vm_block_is_enabled(block))

/** @brief Publish this block's ENO. No-op when the block declares no ENO. */
static inline void vm_block_set_ENO(vm_block_h b, bool state) {
  if (!b->cfg.eno) return;
  uint8_t v = state ? 1 : 0;
  err_h e = VM_OBJ_SET_VAL_AT(v, b->cfg.eno, 0);
  if (unlikely(e)) {
    vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  }
}

/**
 * @brief Run an err_h-returning call inside a block body, reporting failure
 * with this block's identity attached.
 *
 * Block execute() bodies are void -- there is nobody above them to return an
 * err_h to -- so this is where a chain gets reported rather than propagated,
 * the role SE_ORIGIN_CALL plays for top-level entry points. The wrap adds
 * block_idx/block_type, which the accessor-level error underneath cannot know.
 *
 * @code
 * BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), block);
 * @endcode
 */
#define BLOCK_CALL(call, block)                                                                                                                                       \
  do {                                                                                                                                                                \
    err_h __bc_e = (call);                                                                                                                                            \
    if (__bc_e) {                                                                                                                                                     \
      SE_push_to_handler(SE_WRAP_ERR_OWNED(OWNER_VM_BLOCK, __bc_e, ERR_VM_BLOCK_FAILED, .block_idx = (block)->cfg.block_idx, .block_type = (block)->cfg.block_type)); \
    }                                                                                                                                                                 \
  } while (0)
