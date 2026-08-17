/*
* Block expression:
* EN -> not used -> event based (if any IN upd value)
* INPUTS -> 0..15 dynamic: based on bytecode (all fetched as float)
* Outputs: ENO and FLOAT(result)
* IDEA -> simple opcodes wit RPN
* OPCODES PALLETE:
ADD SUB MUL DIV ROOT()N SQARE N, constant-float, + all math operations
and all comparision and negation - NO bit operations

In case of 0 division -> error

all bytecode in payload already in RPN

*/

/*
 * BLOCK BIT expression
 * EN -> not used  -> event based (if any IN upd value)
 * INPUTS -> 0..15 dynamic: based on bytecode (all fetched as UINT32)
 * Outputs: ENO and UINT32(result)
 * IDEA > RPN
 OPCODE PALLETE all bit operations along with neg + constant uint32 in payload
 */

#include "vm_block_expr.h"
#include <math.h>
#include "esp_compiler.h"

#define OWNER OWNER_VM_BLOCK

/*
Two blocks, one machine. The bytecode and its layout are in vm_block_expr.h;
what is here is the evaluator and the three decisions it makes that the format
does not.

  ACTIVATION -- update-driven, and the enable list is not consulted.

  An expression is a function of its inputs: with the same inputs it returns
  the same answer, so re-evaluating one nothing arrived on is work with a known
  result. That makes IF_BLOCK_TRIGGERED the whole activation test, and a
  standing-down pass costs one scan of the pins. A program that wants an
  expression gated wires the gate downstream, where a level belongs.

  INPUTS -- read at most once each, and only if the code asks.

  A pin appearing three times in an expression is one read, latched in `in[]`
  and reused, which keeps an evaluation internally consistent as well as cheap:
  every occurrence of `IN 2` is the same number even though a dynamic accessor
  could resolve twice. And a pin the code never names is never read at all, so
  an unwired pin the program left declared costs nothing rather than failing
  the block -- the same reason the walk is lazy in the first place.

  REPORTING -- two kinds of fault, two lifetimes, because this runs at 100 Hz.

  A malformed program (ERR_VM_EXPR_BAD_CODE) is a standing condition: the same
  byte is wrong on every pass, so it is reported once and latched in
  VM_BLK_RT_CFG_BAD, exactly as a malformed span is. A bad *value*
  (ERR_VM_EXPR_MATH -- a zero divisor, a negative square root) is not: it is
  this pass's data, and it may be gone next pass. Reporting it every pass would
  put six thousand identical errors a minute through the handler and drown
  everything else, so it is reported when the block *enters* a fault episode
  and re-armed by the next evaluation that completes cleanly. A divisor that
  sits at zero for a minute is one error; one that flickers is one error per
  flicker, which is the shape a user can actually read.

  Either way the block publishes nothing on a fault. Its output stands at the
  last value it computed and cfg.on_error decides the flow, which the
  supervisor applies -- never the body (see vm_block_template.h).
*/

/* ==========================================================================
   Faults
   ========================================================================== */

/** Marks the block failed and reports @p e, which is NULL when the same fault
 *  has already been reported and is being suppressed. Always returns false so
 *  an evaluator arm can `return fail(...)`. */
static bool fail(vm_block_h b, err_h e) {
  if (e) {
    vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  } else {
    g_vm_block_fault = true;  // suppressed report, but the block still failed
  }
  return false;
}

/* Latched in cfg.rt rather than in the header's `rt`, because the first thing
   that can be malformed is the header itself -- there may be no byte of ours
   to latch in. */
static err_h bad_code(vm_block_h b, uint16_t pc, uint8_t op, uint8_t reason) {
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return NULL;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;
  SE_RET_ERR(ERR_VM_EXPR_BAD_CODE, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
}

static err_h math_fault(vm_block_h b, vm_expr_code_t* c, uint16_t pc, uint8_t op, uint8_t reason) {
  if (c->rt & VM_EXPR_RT_FAULTED) return NULL;
  c->rt |= VM_EXPR_RT_FAULTED;
  SE_RET_ERR(ERR_VM_EXPR_MATH, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
}

/** The block's code, or NULL if custom_data cannot hold what the header claims
 *  -- checked every pass because custom_len is the program's word and nothing
 *  validates it at load (see the palette note in VM.MD). */
static vm_expr_code_t* code_of(vm_block_h b) {
  if (unlikely(b->cfg.custom_len < sizeof(vm_expr_code_t))) return NULL;
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_custom_data(b);
  if (unlikely(vm_expr_size(c->const_cnt, c->code_len) > b->cfg.custom_len)) return NULL;
  return c;
}

/* ==========================================================================
   The evaluators

   Both walk the same skeleton, so the plumbing is one set of macros: `at` is
   where the current instruction started (what an error names, not the cursor
   past its operand), `op` is what it was, and `st`/`sp` are the stack. UN/BIN
   take the element type from the stack itself, which is the only difference
   between the two loops that the macros would otherwise have to know about.

     WHY THE FAULT PATHS ARE BUILT WHERE THEY STAND

   Each guard constructs and reports its own error inline, which looks like the
   thing to hoist into one epilogue at the bottom -- and measurement says
   otherwise, so the shape is deliberate.

   Hoisting was tried. It does exactly what it promises to the *binary*: the 70
   inlined SE_alloc_bytes + vm_block_report_error pairs collapse to 5, and the
   image loses 3.2 kB. It also made every program **4-6% slower** (473 -> 502
   cyc on a two-pin add, 2832 -> 2945 on the twenty-op case), because an
   epilogue has to be told where it went wrong: `at`, `op`, the reason and the
   err_h all become live across the whole loop.

   That is the binding constraint here, and it is not code size. Xtensa's
   windowed ABI leaves this function about twelve usable registers, and the
   loop already wants pc, sp, the bitmask, the code pointer and the block and
   code pointers live at once -- so it is spilling, and every added live value
   is paid once per bytecode instruction rather than once per fault. Keeping
   `at` and `op` scoped to the switch is what lets them stay in registers.

   Same reasoning against hoisting c->code_len and friends into locals: four
   more live values cost more than the reloads they save.
   ========================================================================== */

#define BAD(reason_) return fail(b, bad_code(b, at, op, (reason_)))
#define MATH(reason_) return fail(b, math_fault(b, c, at, op, (reason_)))
#define TRY(call_)                        \
  do {                                    \
    err_h e_ = (call_);                   \
    if (unlikely(e_)) return fail(b, e_); \
  } while (0)

#define OPERAND(name_)                                         \
  uint8_t name_ = 0;                                           \
  do {                                                         \
    if (unlikely(pc >= c->code_len)) BAD(VM_EXPR_BAD_OPERAND); \
    name_ = code[pc++];                                        \
  } while (0)

#define NEED(n_)                                             \
  do {                                                       \
    if (unlikely(sp < (n_))) BAD(VM_EXPR_BAD_UNDERFLOW);     \
  } while (0)

#define PUSH(v_)                                                  \
  do {                                                            \
    if (unlikely(sp >= VM_EXPR_STACK_MAX)) BAD(VM_EXPR_BAD_OVERFLOW); \
    st[sp++] = (v_);                                              \
  } while (0)

#define UN(expr_)                    \
  do {                               \
    NEED(1);                         \
    __typeof__(st[0]) x = st[sp - 1]; \
    st[sp - 1] = (expr_);            \
  } while (0)

#define BIN(expr_)                       \
  do {                                   \
    NEED(2);                             \
    __typeof__(st[0]) y = st[--sp];      \
    __typeof__(st[0]) x = st[sp - 1];    \
    st[sp - 1] = (expr_);                \
  } while (0)

/* Both stack-plumbing sequences, identical in either word. */
#define CASE_STACK_OPS(END_, DUP_, DROP_, SWAP_)      \
  case END_:                                          \
    pc = c->code_len;                                 \
    break;                                            \
  case DUP_: {                                        \
    NEED(1);                                          \
    __typeof__(st[0]) d = st[sp - 1];                 \
    PUSH(d); /* via a local: st[sp++] = st[sp-1] is unsequenced */ \
    break;                                            \
  }                                                   \
  case DROP_:                                         \
    NEED(1);                                          \
    sp--;                                             \
    break;                                            \
  case SWAP_: {                                       \
    NEED(2);                                          \
    __typeof__(st[0]) t = st[sp - 1];                 \
    st[sp - 1] = st[sp - 2];                          \
    st[sp - 2] = t;                                   \
    break;                                            \
  }

// ---------------------------------------------------------------------------
// float
// ---------------------------------------------------------------------------

/* Guarded so a negative radicand is a fault rather than a NaN -- except for an
   odd integer degree, where the real root exists and powf() would refuse it.
   The caller has already established n != 0 and that x >= 0 or n is odd. */
static bool root_is_odd_int(float n) {
  float t = truncf(n);
  return n == t && fmodf(t, 2.0f) != 0.0f;
}

static float root_f(float x, float n) {
  return (x < 0.0f) ? -powf(-x, 1.0f / n) : powf(x, 1.0f / n);
}

static bool eval_f(vm_block_h b, vm_expr_code_t* c, float* out) {
  const uint8_t* code = vm_expr_bytecode(c);
  float st[VM_EXPR_STACK_MAX];
  float in[VM_BLOCK_MAX_IN];
  uint16_t loaded = 0;  // one bit per pin already read this evaluation
  uint8_t sp = 0;
  uint16_t pc = 0;

  while (pc < c->code_len) {
    const uint16_t at = pc;
    const uint8_t op = code[pc++];

    switch (op) {
      CASE_STACK_OPS(VM_EXPR_END, VM_EXPR_DUP, VM_EXPR_DROP, VM_EXPR_SWAP)

      case VM_EXPR_IN: {
        OPERAND(pin);
        if (unlikely(pin >= b->cfg.in_cnt)) BAD(VM_EXPR_BAD_OPERAND);
        if (!(loaded & (1u << pin))) {
          const vm_accessor_t* acc = NULL;
          TRY(vm_block_get_in(&acc, b, pin));
          TRY(VM_OBJ_GET_VAL(in[pin], acc));
          loaded |= (uint16_t)(1u << pin);
        }
        PUSH(in[pin]);
        break;
      }
      case VM_EXPR_K: {
        OPERAND(k);
        if (unlikely(k >= c->const_cnt)) BAD(VM_EXPR_BAD_OPERAND);
        PUSH(c->consts[k].f);
        break;
      }

      case VM_EXPR_ADD: BIN(x + y); break;
      case VM_EXPR_SUB: BIN(x - y); break;
      case VM_EXPR_MUL: BIN(x * y); break;
      case VM_EXPR_DIV:
        NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) MATH(VM_EXPR_MATH_DIV0);
        BIN(x / y);
        break;
      case VM_EXPR_MOD:
        NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) MATH(VM_EXPR_MATH_DIV0);
        BIN(fmodf(x, y));
        break;
      case VM_EXPR_POW:
        NEED(2);
        // a negative base only has a real power for an integer exponent
        if (unlikely(st[sp - 2] < 0.0f && st[sp - 1] != truncf(st[sp - 1]))) MATH(VM_EXPR_MATH_DOMAIN);
        BIN(powf(x, y));
        break;
      case VM_EXPR_ROOT:
        NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) MATH(VM_EXPR_MATH_DIV0);
        if (unlikely(st[sp - 2] < 0.0f && !root_is_odd_int(st[sp - 1]))) MATH(VM_EXPR_MATH_DOMAIN);
        BIN(root_f(x, y));
        break;
      case VM_EXPR_MIN: BIN(fminf(x, y)); break;
      case VM_EXPR_MAX: BIN(fmaxf(x, y)); break;
      case VM_EXPR_ATAN2: BIN(atan2f(x, y)); break;
      case VM_EXPR_HYPOT: BIN(hypotf(x, y)); break;

      case VM_EXPR_NEG: UN(-x); break;
      case VM_EXPR_ABS: UN(fabsf(x)); break;
      case VM_EXPR_SQRT:
        NEED(1);
        if (unlikely(st[sp - 1] < 0.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(sqrtf(x));
        break;
      case VM_EXPR_SQUARE: UN(x * x); break;
      case VM_EXPR_RECIP:
        NEED(1);
        if (unlikely(st[sp - 1] == 0.0f)) MATH(VM_EXPR_MATH_DIV0);
        UN(1.0f / x);
        break;
      case VM_EXPR_FLOOR: UN(floorf(x)); break;
      case VM_EXPR_CEIL: UN(ceilf(x)); break;
      case VM_EXPR_ROUND: UN(roundf(x)); break;
      case VM_EXPR_TRUNC: UN(truncf(x)); break;
      case VM_EXPR_SIGN: UN(x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f)); break;
      case VM_EXPR_SIN: UN(sinf(x)); break;
      case VM_EXPR_COS: UN(cosf(x)); break;
      case VM_EXPR_TAN: UN(tanf(x)); break;
      case VM_EXPR_ASIN:
        NEED(1);
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(asinf(x));
        break;
      case VM_EXPR_ACOS:
        NEED(1);
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(acosf(x));
        break;
      case VM_EXPR_ATAN: UN(atanf(x)); break;
      case VM_EXPR_EXP: UN(expf(x)); break;
      case VM_EXPR_LOG:
        NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(logf(x));
        break;
      case VM_EXPR_LOG10:
        NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(log10f(x));
        break;
      case VM_EXPR_LOG2:
        NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) MATH(VM_EXPR_MATH_DOMAIN);
        UN(log2f(x));
        break;
      case VM_EXPR_DEG: UN(x * (180.0f / (float)M_PI)); break;
      case VM_EXPR_RAD: UN(x * ((float)M_PI / 180.0f)); break;

      case VM_EXPR_LT: BIN(x < y ? 1.0f : 0.0f); break;
      case VM_EXPR_LE: BIN(x <= y ? 1.0f : 0.0f); break;
      case VM_EXPR_GT: BIN(x > y ? 1.0f : 0.0f); break;
      case VM_EXPR_GE: BIN(x >= y ? 1.0f : 0.0f); break;
      case VM_EXPR_EQ: BIN(x == y ? 1.0f : 0.0f); break;
      case VM_EXPR_NE: BIN(x != y ? 1.0f : 0.0f); break;

      case VM_EXPR_AND: BIN((x != 0.0f && y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_OR: BIN((x != 0.0f || y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_XOR: BIN(((x != 0.0f) != (y != 0.0f)) ? 1.0f : 0.0f); break;
      case VM_EXPR_NOT: UN(x == 0.0f ? 1.0f : 0.0f); break;

      case VM_EXPR_SEL: {
        NEED(3);
        float f_arm = st[--sp];
        float t_arm = st[--sp];
        st[sp - 1] = (st[sp - 1] != 0.0f) ? t_arm : f_arm;
        break;
      }

      default:
        BAD(VM_EXPR_BAD_OPCODE);
    }
  }

  if (unlikely(sp != 1)) return fail(b, bad_code(b, c->code_len, VM_EXPR_END, VM_EXPR_BAD_RESULT));

  /* The backstop for everything the per-op guards do not name: an overflow to
     infinity, a NaN out of tanf() at a pole. Publishing one would push it
     through every block downstream, where it survives arithmetic and quietly
     wins comparisons. */
  if (unlikely(!isfinite(st[0]))) {
    return fail(b, math_fault(b, c, c->code_len, VM_EXPR_END, VM_EXPR_MATH_NOT_FINITE));
  }

  *out = st[0];
  return true;
}

// ---------------------------------------------------------------------------
// uint32
// ---------------------------------------------------------------------------

static uint32_t rotl32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x << n) | (x >> (32u - n))) : x;
}

static uint32_t rotr32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x >> n) | (x << (32u - n))) : x;
}

static bool eval_bit(vm_block_h b, vm_expr_code_t* c, uint32_t* out) {
  const uint8_t* code = vm_expr_bytecode(c);
  uint32_t st[VM_EXPR_STACK_MAX];
  uint32_t in[VM_BLOCK_MAX_IN];
  uint16_t loaded = 0;
  uint8_t sp = 0;
  uint16_t pc = 0;

  while (pc < c->code_len) {
    const uint16_t at = pc;
    const uint8_t op = code[pc++];

    switch (op) {
      CASE_STACK_OPS(VM_BIT_END, VM_BIT_DUP, VM_BIT_DROP, VM_BIT_SWAP)

      case VM_BIT_IN: {
        OPERAND(pin);
        if (unlikely(pin >= b->cfg.in_cnt)) BAD(VM_EXPR_BAD_OPERAND);
        if (!(loaded & (1u << pin))) {
          const vm_accessor_t* acc = NULL;
          TRY(vm_block_get_in(&acc, b, pin));
          TRY(VM_OBJ_GET_VAL(in[pin], acc));
          loaded |= (uint16_t)(1u << pin);
        }
        PUSH(in[pin]);
        break;
      }
      case VM_BIT_K: {
        OPERAND(k);
        if (unlikely(k >= c->const_cnt)) BAD(VM_EXPR_BAD_OPERAND);
        PUSH(c->consts[k].u);
        break;
      }

      case VM_BIT_AND: BIN(x & y); break;
      case VM_BIT_OR: BIN(x | y); break;
      case VM_BIT_XOR: BIN(x ^ y); break;
      case VM_BIT_SHL: BIN(y >= 32u ? 0u : (x << y)); break;
      case VM_BIT_SHR: BIN(y >= 32u ? 0u : (x >> y)); break;
      /* GCC arithmetic-shifts a signed right shift, which is what SAR is for;
         the >= 32 arm spells out the sign fill C leaves undefined. */
      case VM_BIT_SAR: BIN(y >= 32u ? (uint32_t)((int32_t)x >> 31) : (uint32_t)((int32_t)x >> y)); break;
      case VM_BIT_ROL: BIN(rotl32(x, y)); break;
      case VM_BIT_ROR: BIN(rotr32(x, y)); break;
      case VM_BIT_GET: BIN((x >> (y & 31u)) & 1u); break;
      case VM_BIT_SET: BIN(x | (1u << (y & 31u))); break;
      case VM_BIT_CLR: BIN(x & ~(1u << (y & 31u))); break;
      case VM_BIT_TGL: BIN(x ^ (1u << (y & 31u))); break;

      case VM_BIT_NOT: UN(~x); break;
      case VM_BIT_POPCNT: UN((uint32_t)__builtin_popcount(x)); break;
      case VM_BIT_CLZ: UN(x ? (uint32_t)__builtin_clz(x) : 32u); break;
      case VM_BIT_CTZ: UN(x ? (uint32_t)__builtin_ctz(x) : 32u); break;
      case VM_BIT_BSWAP: UN(__builtin_bswap32(x)); break;

      default:
        BAD(VM_EXPR_BAD_OPCODE);
    }
  }

  /* No math fault anywhere in this palette: every operation is total -- a
     shift of 32 or more is 0 and a rotate takes its count mod 32, so there is
     no bad value here, only bad code. */
  if (unlikely(sp != 1)) return fail(b, bad_code(b, c->code_len, VM_BIT_END, VM_EXPR_BAD_RESULT));

  *out = st[0];
  return true;
}

#undef BAD
#undef MATH
#undef TRY
#undef OPERAND
#undef NEED
#undef PUSH
#undef UN
#undef BIN
#undef CASE_STACK_OPS

/* ==========================================================================
   The bodies
   ========================================================================== */

/* Everything both blocks do around their evaluator, which is everything except
   the evaluator: the shape check, the activation test, publishing, and the one
   ENO rule. `eval` returns false having already reported, so the only thing
   left to decide here is whether the flow may be asserted -- and it may not if
   *anything* in this call failed, which is what g_vm_block_fault records. ENO
   is never dropped from here: a body that failed cannot know whether it is
   about to fail again, so cfg.on_error is the supervisor's to apply. */
static void run_expr(vm_block_h b, bool bitwise) {
  vm_expr_code_t* c = code_of(b);
  if (unlikely(!c)) {
    (void)fail(b, bad_code(b, 0, 0, VM_EXPR_BAD_HEADER));
    return;
  }
  if (unlikely(b->cfg.q_cnt == 0)) {
    (void)fail(b, bad_code(b, 0, 0, VM_EXPR_BAD_SHAPE));
    return;
  }

  IF_BLOCK_TRIGGERED(b) {
    vm_obj_h q = vm_block_outputs(b)[0];
    if (bitwise) {
      uint32_t r = 0;
      if (unlikely(!eval_bit(b, c, &r))) return;
      BLOCK_CALL(VM_OBJ_SET_VAL_AT(r, q, 0), b);
    } else {
      float r = 0.0f;
      if (unlikely(!eval_f(b, c, &r))) return;
      BLOCK_CALL(VM_OBJ_SET_VAL_AT(r, q, 0), b);
    }

    // an evaluation that reached here is a clean one: re-arm math reporting
    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!g_vm_block_fault)) vm_block_set_ENO(b, true);
    return;
  }

  vm_block_set_ENO(b, false);
}

void vm_blk_expr(vm_block_h b) {
  run_expr(b, false);
}

void vm_blk_expr_bit(vm_block_h b) {
  run_expr(b, true);
}
