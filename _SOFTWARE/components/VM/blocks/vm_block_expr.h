#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_compiler.h"
#include "vm_block.h"

/*
RPN Bytecode Expression Engine (VM_BLK_EXPR for float, VM_BLK_EXPR_BIT for uint32).
Layout in custom_data:
  [0]     u8  const_cnt         Literals count following header
  [1]     u8  rt                Runtime latch (VM_EXPR_RT_FAULTED; 0 on wire)
  [2..3]  u16 code_len          Bytecode length in bytes
  [4..]   u32 consts[const_cnt] 4-aligned literals
  [...]   u8  code[code_len]    RPN instruction stream (IN and K have +1 operand byte)
*/

#define VM_EXPR_STACK_MAX 16

typedef union vm_expr_k_t {
  uint32_t u;
  float f;
} vm_expr_k_t;

typedef struct vm_expr_code_t {
  uint8_t const_cnt;
  uint8_t rt;
  uint16_t code_len;
  vm_expr_k_t consts[];
} vm_expr_code_t;

_Static_assert(sizeof(vm_expr_code_t) == 4, "header must stay 4 bytes for literal alignment");

#define VM_EXPR_RT_FAULTED 0x01u

static inline size_t vm_expr_size(uint8_t const_cnt, uint16_t code_len) {
  return sizeof(vm_expr_code_t) + (size_t)const_cnt * sizeof(vm_expr_k_t) + (size_t)code_len;
}

static inline const uint8_t* vm_expr_bytecode(const vm_expr_code_t* c) {
  return (const uint8_t*)&c->consts[c->const_cnt];
}

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR (float)
   ========================================================================== */
typedef enum vm_expr_op_e {
  VM_EXPR_END = 0,
  VM_EXPR_IN,
  VM_EXPR_K,
  VM_EXPR_DUP,
  VM_EXPR_DROP,
  VM_EXPR_SWAP,

  // binary arithmetic
  VM_EXPR_ADD,
  VM_EXPR_SUB,
  VM_EXPR_MUL,
  VM_EXPR_DIV,
  VM_EXPR_MOD,
  VM_EXPR_POW,
  VM_EXPR_ROOT,
  VM_EXPR_MIN,
  VM_EXPR_MAX,
  VM_EXPR_ATAN2,
  VM_EXPR_HYPOT,

  // unary
  VM_EXPR_NEG,
  VM_EXPR_ABS,
  VM_EXPR_SQRT,
  VM_EXPR_SQUARE,
  VM_EXPR_RECIP,
  VM_EXPR_FLOOR,
  VM_EXPR_CEIL,
  VM_EXPR_ROUND,
  VM_EXPR_TRUNC,
  VM_EXPR_SIGN,
  VM_EXPR_SIN,
  VM_EXPR_COS,
  VM_EXPR_TAN,
  VM_EXPR_ASIN,
  VM_EXPR_ACOS,
  VM_EXPR_ATAN,
  VM_EXPR_EXP,
  VM_EXPR_LOG,
  VM_EXPR_LOG10,
  VM_EXPR_LOG2,
  VM_EXPR_DEG,
  VM_EXPR_RAD,

  // comparison
  VM_EXPR_LT,
  VM_EXPR_LE,
  VM_EXPR_GT,
  VM_EXPR_GE,
  VM_EXPR_EQ,
  VM_EXPR_NE,

  // logic
  VM_EXPR_AND,
  VM_EXPR_OR,
  VM_EXPR_XOR,
  VM_EXPR_NOT,

  VM_EXPR_SEL,
  VM_EXPR_OP_CNT
} vm_expr_op_e;

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR_BIT (uint32)
   ========================================================================== */
typedef enum vm_bit_op_e {
  VM_BIT_END = 0,
  VM_BIT_IN,
  VM_BIT_K,
  VM_BIT_DUP,
  VM_BIT_DROP,
  VM_BIT_SWAP,

  // binary
  VM_BIT_AND,
  VM_BIT_OR,
  VM_BIT_XOR,
  VM_BIT_SHL,
  VM_BIT_SHR,
  VM_BIT_SAR,
  VM_BIT_ROL,
  VM_BIT_ROR,
  VM_BIT_GET,
  VM_BIT_SET,
  VM_BIT_CLR,
  VM_BIT_TGL,

  // unary
  VM_BIT_NOT,
  VM_BIT_POPCNT,
  VM_BIT_CLZ,
  VM_BIT_CTZ,
  VM_BIT_BSWAP,

  VM_BIT_OP_CNT
} vm_bit_op_e;

/* ==========================================================================
   Fault reasons
   ========================================================================== */
#define VM_EXPR_BAD_HEADER 0u
#define VM_EXPR_BAD_SHAPE 1u
#define VM_EXPR_BAD_OPCODE 2u
#define VM_EXPR_BAD_OPERAND 3u
#define VM_EXPR_BAD_UNDERFLOW 4u
#define VM_EXPR_BAD_OVERFLOW 5u
#define VM_EXPR_BAD_RESULT 6u

#define VM_EXPR_MATH_DIV0 0u
#define VM_EXPR_MATH_DOMAIN 1u
#define VM_EXPR_MATH_NOT_FINITE 2u

/* ==========================================================================
   Evaluator Implementation
   ========================================================================== */

static inline bool vm_expr_fail(vm_block_h b, err_h e) {
  if (e) {
    vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  } else {
    g_vm_block_fault = true;
  }
  return false;
}

static inline err_h vm_expr_bad_code(vm_block_h b, uint16_t pc, uint8_t op, uint8_t reason) {
  if (b->cfg.rt & VM_BLK_RT_CFG_BAD) return NULL;
  b->cfg.rt |= VM_BLK_RT_CFG_BAD;
  return VM_BLK_ERR_NEW(ERR_VM_EXPR_BAD_CODE, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
}

static inline err_h vm_expr_math_fault(vm_block_h b, vm_expr_code_t* c, uint16_t pc, uint8_t op, uint8_t reason) {
  if (c->rt & VM_EXPR_RT_FAULTED) return NULL;
  c->rt |= VM_EXPR_RT_FAULTED;
  return VM_BLK_ERR_NEW(ERR_VM_EXPR_MATH, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
}

static inline vm_expr_code_t* vm_expr_code_of(vm_block_h b) {
  if (unlikely(b->cfg.custom_len < sizeof(vm_expr_code_t))) return NULL;
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);
  if (unlikely(vm_expr_size(c->const_cnt, c->code_len) > b->cfg.custom_len)) return NULL;
  return c;
}

#define _BAD(reason_) return vm_expr_fail(b, vm_expr_bad_code(b, at, op, (reason_)))
#define _MATH(reason_) return vm_expr_fail(b, vm_expr_math_fault(b, c, at, op, (reason_)))
#define _TRY(call_)                             \
  do {                                         \
    err_h e_ = (call_);                        \
    if (unlikely(e_)) return vm_expr_fail(b, e_); \
  } while (0)

#define _OPERAND(name_)                                         \
  uint8_t name_ = 0;                                           \
  do {                                                         \
    if (unlikely(pc >= c->code_len)) _BAD(VM_EXPR_BAD_OPERAND); \
    name_ = code[pc++];                                        \
  } while (0)

#define _NEED(n_)                                             \
  do {                                                       \
    if (unlikely(sp < (n_))) _BAD(VM_EXPR_BAD_UNDERFLOW);     \
  } while (0)

#define _PUSH(v_)                                                  \
  do {                                                            \
    if (unlikely(sp >= VM_EXPR_STACK_MAX)) _BAD(VM_EXPR_BAD_OVERFLOW); \
    st[sp++] = (v_);                                              \
  } while (0)

#define _UN(expr_)                   \
  do {                               \
    _NEED(1);                        \
    __typeof__(st[0]) x = st[sp - 1]; \
    st[sp - 1] = (expr_);            \
  } while (0)

#define _BIN(expr_)                      \
  do {                                   \
    _NEED(2);                            \
    __typeof__(st[0]) y = st[--sp];      \
    __typeof__(st[0]) x = st[sp - 1];    \
    st[sp - 1] = (expr_);                \
  } while (0)

#define _CASE_STACK_OPS(END_, DUP_, DROP_, SWAP_)      \
  case END_:                                           \
    pc = c->code_len;                                  \
    break;                                             \
  case DUP_: {                                         \
    _NEED(1);                                          \
    __typeof__(st[0]) d = st[sp - 1];                  \
    _PUSH(d);                                          \
    break;                                             \
  }                                                    \
  case DROP_:                                          \
    _NEED(1);                                          \
    sp--;                                              \
    break;                                             \
  case SWAP_: {                                        \
    _NEED(2);                                          \
    __typeof__(st[0]) t = st[sp - 1];                  \
    st[sp - 1] = st[sp - 2];                           \
    st[sp - 2] = t;                                    \
    break;                                             \
  }

static inline bool vm_expr_root_is_odd_int(float n) {
  float t = truncf(n);
  return n == t && fmodf(t, 2.0f) != 0.0f;
}

static inline float vm_expr_root_f(float x, float n) {
  return (x < 0.0f) ? -powf(-x, 1.0f / n) : powf(x, 1.0f / n);
}

static inline bool vm_expr_eval_f(vm_block_h b, vm_expr_code_t* c, float* out) {
  const uint8_t* code = vm_expr_bytecode(c);
  float st[VM_EXPR_STACK_MAX];
  float in[VM_BLOCK_MAX_IN];
  uint16_t loaded = 0;
  uint8_t sp = 0;
  uint16_t pc = 0;

  while (pc < c->code_len) {
    const uint16_t at = pc;
    const uint8_t op = code[pc++];

    switch (op) {
      _CASE_STACK_OPS(VM_EXPR_END, VM_EXPR_DUP, VM_EXPR_DROP, VM_EXPR_SWAP)

      case VM_EXPR_IN: {
        _OPERAND(pin);
        if (unlikely(pin >= b->cfg.in_cnt)) _BAD(VM_EXPR_BAD_OPERAND);
        uint16_t mask = (uint16_t)(1u << pin);
        if (!(loaded & mask)) {
          const vm_accessor_t* acc = vm_block_get_inputs(b)[pin];
          if (unlikely(!acc)) _BAD(VM_EXPR_BAD_OPERAND);
          if (likely((acc->flags & VM_ACC_F_CACHED) && (acc->c_payload.type == VM_OBJ_F))) {
            in[pin] = *(const float*)acc->c_payload.ptr;
          } else {
            _TRY(VM_OBJ_GET_VAL(in[pin], acc));
          }
          loaded |= mask;
        }
        _PUSH(in[pin]);
        break;
      }
      case VM_EXPR_K: {
        _OPERAND(k);
        if (unlikely(k >= c->const_cnt)) _BAD(VM_EXPR_BAD_OPERAND);
        _PUSH(c->consts[k].f);
        break;
      }

      case VM_EXPR_ADD: _BIN(x + y); break;
      case VM_EXPR_SUB: _BIN(x - y); break;
      case VM_EXPR_MUL: _BIN(x * y); break;
      case VM_EXPR_DIV:
        _NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _BIN(x / y);
        break;
      case VM_EXPR_MOD:
        _NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _BIN(fmodf(x, y));
        break;
      case VM_EXPR_POW:
        _NEED(2);
        if (unlikely(st[sp - 2] < 0.0f && st[sp - 1] != truncf(st[sp - 1]))) _MATH(VM_EXPR_MATH_DOMAIN);
        _BIN(powf(x, y));
        break;
      case VM_EXPR_ROOT:
        _NEED(2);
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        if (unlikely(st[sp - 2] < 0.0f && !vm_expr_root_is_odd_int(st[sp - 1]))) _MATH(VM_EXPR_MATH_DOMAIN);
        _BIN(vm_expr_root_f(x, y));
        break;
      case VM_EXPR_MIN: _BIN(fminf(x, y)); break;
      case VM_EXPR_MAX: _BIN(fmaxf(x, y)); break;
      case VM_EXPR_ATAN2: _BIN(atan2f(x, y)); break;
      case VM_EXPR_HYPOT: _BIN(hypotf(x, y)); break;

      case VM_EXPR_NEG: _UN(-x); break;
      case VM_EXPR_ABS: _UN(fabsf(x)); break;
      case VM_EXPR_SQRT:
        _NEED(1);
        if (unlikely(st[sp - 1] < 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(sqrtf(x));
        break;
      case VM_EXPR_SQUARE: _UN(x * x); break;
      case VM_EXPR_RECIP:
        _NEED(1);
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _UN(1.0f / x);
        break;
      case VM_EXPR_FLOOR: _UN(floorf(x)); break;
      case VM_EXPR_CEIL: _UN(ceilf(x)); break;
      case VM_EXPR_ROUND: _UN(roundf(x)); break;
      case VM_EXPR_TRUNC: _UN(truncf(x)); break;
      case VM_EXPR_SIGN: _UN(x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f)); break;
      case VM_EXPR_SIN: _UN(sinf(x)); break;
      case VM_EXPR_COS: _UN(cosf(x)); break;
      case VM_EXPR_TAN: _UN(tanf(x)); break;
      case VM_EXPR_ASIN:
        _NEED(1);
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(asinf(x));
        break;
      case VM_EXPR_ACOS:
        _NEED(1);
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(acosf(x));
        break;
      case VM_EXPR_ATAN: _UN(atanf(x)); break;
      case VM_EXPR_EXP: _UN(expf(x)); break;
      case VM_EXPR_LOG:
        _NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(logf(x));
        break;
      case VM_EXPR_LOG10:
        _NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(log10f(x));
        break;
      case VM_EXPR_LOG2:
        _NEED(1);
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(log2f(x));
        break;
      case VM_EXPR_DEG: _UN(x * (180.0f / (float)M_PI)); break;
      case VM_EXPR_RAD: _UN(x * ((float)M_PI / 180.0f)); break;

      case VM_EXPR_LT: _BIN(x < y ? 1.0f : 0.0f); break;
      case VM_EXPR_LE: _BIN(x <= y ? 1.0f : 0.0f); break;
      case VM_EXPR_GT: _BIN(x > y ? 1.0f : 0.0f); break;
      case VM_EXPR_GE: _BIN(x >= y ? 1.0f : 0.0f); break;
      case VM_EXPR_EQ: _BIN(x == y ? 1.0f : 0.0f); break;
      case VM_EXPR_NE: _BIN(x != y ? 1.0f : 0.0f); break;

      case VM_EXPR_AND: _BIN((x != 0.0f && y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_OR: _BIN((x != 0.0f || y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_XOR: _BIN(((x != 0.0f) != (y != 0.0f)) ? 1.0f : 0.0f); break;
      case VM_EXPR_NOT: _UN(x == 0.0f ? 1.0f : 0.0f); break;

      case VM_EXPR_SEL: {
        _NEED(3);
        float f_arm = st[--sp];
        float t_arm = st[--sp];
        st[sp - 1] = (st[sp - 1] != 0.0f) ? t_arm : f_arm;
        break;
      }

      default:
        _BAD(VM_EXPR_BAD_OPCODE);
    }
  }

  if (unlikely(sp != 1)) return vm_expr_fail(b, vm_expr_bad_code(b, c->code_len, VM_EXPR_END, VM_EXPR_BAD_RESULT));
  if (unlikely(!isfinite(st[0]))) {
    return vm_expr_fail(b, vm_expr_math_fault(b, c, c->code_len, VM_EXPR_END, VM_EXPR_MATH_NOT_FINITE));
  }

  *out = st[0];
  return true;
}

static inline uint32_t vm_expr_rotl32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x << n) | (x >> (32u - n))) : x;
}

static inline uint32_t vm_expr_rotr32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x >> n) | (x << (32u - n))) : x;
}

static inline bool vm_expr_eval_bit(vm_block_h b, vm_expr_code_t* c, uint32_t* out) {
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
      _CASE_STACK_OPS(VM_BIT_END, VM_BIT_DUP, VM_BIT_DROP, VM_BIT_SWAP)

      case VM_BIT_IN: {
        _OPERAND(pin);
        if (unlikely(pin >= b->cfg.in_cnt)) _BAD(VM_EXPR_BAD_OPERAND);
        uint16_t mask = (uint16_t)(1u << pin);
        if (!(loaded & mask)) {
          const vm_accessor_t* acc = vm_block_get_inputs(b)[pin];
          if (unlikely(!acc)) _BAD(VM_EXPR_BAD_OPERAND);
          if (likely((acc->flags & VM_ACC_F_CACHED) && (acc->c_payload.type == VM_OBJ_U32))) {
            in[pin] = *(const uint32_t*)acc->c_payload.ptr;
          } else {
            _TRY(VM_OBJ_GET_VAL(in[pin], acc));
          }
          loaded |= mask;
        }
        _PUSH(in[pin]);
        break;
      }
      case VM_BIT_K: {
        _OPERAND(k);
        if (unlikely(k >= c->const_cnt)) _BAD(VM_EXPR_BAD_OPERAND);
        _PUSH(c->consts[k].u);
        break;
      }

      case VM_BIT_AND: _BIN(x & y); break;
      case VM_BIT_OR: _BIN(x | y); break;
      case VM_BIT_XOR: _BIN(x ^ y); break;
      case VM_BIT_SHL: _BIN(y >= 32u ? 0u : (x << y)); break;
      case VM_BIT_SHR: _BIN(y >= 32u ? 0u : (x >> y)); break;
      case VM_BIT_SAR: _BIN(y >= 32u ? (uint32_t)((int32_t)x >> 31) : (uint32_t)((int32_t)x >> y)); break;
      case VM_BIT_ROL: _BIN(vm_expr_rotl32(x, y)); break;
      case VM_BIT_ROR: _BIN(vm_expr_rotr32(x, y)); break;
      case VM_BIT_GET: _BIN((x >> (y & 31u)) & 1u); break;
      case VM_BIT_SET: _BIN(x | (1u << (y & 31u))); break;
      case VM_BIT_CLR: _BIN(x & ~(1u << (y & 31u))); break;
      case VM_BIT_TGL: _BIN(x ^ (1u << (y & 31u))); break;

      case VM_BIT_NOT: _UN(~x); break;
      case VM_BIT_POPCNT: _UN((uint32_t)__builtin_popcount(x)); break;
      case VM_BIT_CLZ: _UN(x ? (uint32_t)__builtin_clz(x) : 32u); break;
      case VM_BIT_CTZ: _UN(x ? (uint32_t)__builtin_ctz(x) : 32u); break;
      case VM_BIT_BSWAP: _UN(__builtin_bswap32(x)); break;

      default:
        _BAD(VM_EXPR_BAD_OPCODE);
    }
  }

  if (unlikely(sp != 1)) return vm_expr_fail(b, vm_expr_bad_code(b, c->code_len, VM_BIT_END, VM_EXPR_BAD_RESULT));

  *out = st[0];
  return true;
}

#undef _BAD
#undef _MATH
#undef _TRY
#undef _OPERAND
#undef _NEED
#undef _PUSH
#undef _UN
#undef _BIN
#undef _CASE_STACK_OPS

static inline bool vm_verify_expr(vm_block_h b) {
  return b->cfg.q_cnt >= 1;
}

static inline void vm_blk_expr(vm_block_h b) {
  vm_expr_code_t* c = vm_expr_code_of(b);
  if (unlikely(!c)) {
    (void)vm_expr_fail(b, vm_expr_bad_code(b, 0, 0, VM_EXPR_BAD_HEADER));
    return;
  }

  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    float r = 0.0f;
    if (unlikely(!vm_expr_eval_f(b, c, &r))) return;
    vm_obj_h q = vm_block_get_outputs(b)[0];
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(r, q, 0), b);

    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!g_vm_block_fault)) vm_block_set_eno(b, true);
    return;
  }

  vm_block_set_eno(b, false);
}

static inline void vm_blk_expr_bit(vm_block_h b) {
  vm_expr_code_t* c = vm_expr_code_of(b);
  if (unlikely(!c)) {
    (void)vm_expr_fail(b, vm_expr_bad_code(b, 0, 0, VM_EXPR_BAD_HEADER));
    return;
  }

  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    uint32_t r = 0;
    if (unlikely(!vm_expr_eval_bit(b, c, &r))) return;
    vm_obj_h q = vm_block_get_outputs(b)[0];
    BLOCK_CALL(VM_OBJ_SET_VAL_AT(r, q, 0), b);

    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!g_vm_block_fault)) vm_block_set_eno(b, true);
    return;
  }

  vm_block_set_eno(b, false);
}
