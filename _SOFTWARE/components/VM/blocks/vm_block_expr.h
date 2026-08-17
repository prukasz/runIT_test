#pragma once
#include <stddef.h>
#include <stdint.h>
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

/** @brief Deepest the evaluation stack may go. An expression needing more is
 *  rejected at run time (VM_EXPR_BAD_OVERFLOW) rather than growing: the block
 *  runs inside a section, so its stack is the supervisor task's. */
#define VM_EXPR_STACK_MAX 16

/** @brief One literal. A union rather than a cast so reading a float out of
 *  the u32 the wire carries is defined, not aliasing luck. */
typedef union vm_expr_k_t {
  uint32_t u;
  float f;
} vm_expr_k_t;

/** @brief The head of an expression block's custom_data -- see the layout above. */
typedef struct vm_expr_code_t {
  uint8_t const_cnt;      // [0]    literals following this header
  uint8_t rt;             // [1]    runtime latch (VM_EXPR_RT_*); 0 on the wire
  uint16_t code_len;      // [2..3] bytecode length in bytes
  vm_expr_k_t consts[];   // [4..]  const_cnt literals, then code_len bytes
} vm_expr_code_t;

_Static_assert(sizeof(vm_expr_code_t) == 4, "the header must stay 4 bytes, or the literals lose their alignment");

/** @brief Sticky within one fault episode: an arithmetic fault has been
 *  reported and further identical ones are not, until an evaluation completes
 *  cleanly and re-arms it. */
#define VM_EXPR_RT_FAULTED 0x01u

/** @brief Bytes of custom_data an expression of this shape needs. */
static inline size_t vm_expr_size(uint8_t const_cnt, uint16_t code_len) {
  return sizeof(vm_expr_code_t) + (size_t)const_cnt * sizeof(vm_expr_k_t) + (size_t)code_len;
}

/** @brief The instruction stream, which begins where the literals end. */
static inline const uint8_t* vm_expr_bytecode(const vm_expr_code_t* c) {
  return (const uint8_t*)&c->consts[c->const_cnt];
}

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR (float)

   Dense and sequential so the dispatch switch compiles to a jump table rather
   than a chain of compares. The first six are numbered identically in both
   palettes on purpose: an editor's encoder, a disassembler and the operand
   check all treat "push" and "stack plumbing" the same way regardless of which
   block they are aimed at.

   Comparisons and the logical ops push 1.0f or 0.0f, so they compose with
   `SEL` and drop straight into a boolean output object. `EQ` is an exact
   float compare -- the editor should be offering `|a-b| < eps` instead, which
   is three instructions here and not something the VM should guess at.
   ========================================================================== */
typedef enum vm_expr_op_e {
  VM_EXPR_END = 0,  // stop; optional, code_len already terminates the walk
  VM_EXPR_IN,       // +u8 pin: push input pin's value
  VM_EXPR_K,        // +u8 idx: push literal
  VM_EXPR_DUP,      // x -> x x
  VM_EXPR_DROP,     // x ->
  VM_EXPR_SWAP,     // x y -> y x

  // binary arithmetic (x y -> result)
  VM_EXPR_ADD,
  VM_EXPR_SUB,
  VM_EXPR_MUL,
  VM_EXPR_DIV,    // y == 0 faults
  VM_EXPR_MOD,    // fmodf; y == 0 faults
  VM_EXPR_POW,    // x^y; negative x with fractional y faults
  VM_EXPR_ROOT,   // y-th root of x; y == 0 faults, negative x needs odd integer y
  VM_EXPR_MIN,
  VM_EXPR_MAX,
  VM_EXPR_ATAN2,  // atan2f(x, y)
  VM_EXPR_HYPOT,

  // unary (x -> result)
  VM_EXPR_NEG,
  VM_EXPR_ABS,
  VM_EXPR_SQRT,  // negative x faults
  VM_EXPR_SQUARE,
  VM_EXPR_RECIP,  // 1/x; x == 0 faults
  VM_EXPR_FLOOR,
  VM_EXPR_CEIL,
  VM_EXPR_ROUND,
  VM_EXPR_TRUNC,
  VM_EXPR_SIGN,  // -1, 0 or 1
  VM_EXPR_SIN,
  VM_EXPR_COS,
  VM_EXPR_TAN,
  VM_EXPR_ASIN,   // |x| > 1 faults
  VM_EXPR_ACOS,   // |x| > 1 faults
  VM_EXPR_ATAN,
  VM_EXPR_EXP,
  VM_EXPR_LOG,    // x <= 0 faults
  VM_EXPR_LOG10,  // x <= 0 faults
  VM_EXPR_LOG2,   // x <= 0 faults
  VM_EXPR_DEG,    // radians -> degrees
  VM_EXPR_RAD,    // degrees -> radians

  // comparison (x y -> 1.0f / 0.0f)
  VM_EXPR_LT,
  VM_EXPR_LE,
  VM_EXPR_GT,
  VM_EXPR_GE,
  VM_EXPR_EQ,
  VM_EXPR_NE,

  // logic, on truthiness (non-zero)
  VM_EXPR_AND,
  VM_EXPR_OR,
  VM_EXPR_XOR,
  VM_EXPR_NOT,

  VM_EXPR_SEL,  // cond a b -> (cond ? a : b); both arms already evaluated

  VM_EXPR_OP_CNT
} vm_expr_op_e;

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR_BIT (uint32)

   No arithmetic and no arithmetic faults: every operation here is total. A
   shift of 32 or more yields 0 rather than the undefined behaviour C gives it,
   and a rotate takes its count mod 32, so a program cannot phrase a shift the
   machine has no answer for.
   ========================================================================== */
typedef enum vm_bit_op_e {
  VM_BIT_END = 0,  // numbered as in vm_expr_op_e -- see the note above
  VM_BIT_IN,       // +u8 pin
  VM_BIT_K,        // +u8 idx
  VM_BIT_DUP,
  VM_BIT_DROP,
  VM_BIT_SWAP,

  // binary (x y -> result)
  VM_BIT_AND,
  VM_BIT_OR,
  VM_BIT_XOR,
  VM_BIT_SHL,  // y >= 32 -> 0
  VM_BIT_SHR,  // logical; y >= 32 -> 0
  VM_BIT_SAR,  // arithmetic, x read as int32; y >= 32 -> sign
  VM_BIT_ROL,  // count is y & 31
  VM_BIT_ROR,
  VM_BIT_GET,  // bit y of x, as 0 or 1
  VM_BIT_SET,  // x with bit y set
  VM_BIT_CLR,
  VM_BIT_TGL,

  // unary (x -> result)
  VM_BIT_NOT,  // ~x
  VM_BIT_POPCNT,
  VM_BIT_CLZ,  // 32 for x == 0
  VM_BIT_CTZ,  // 32 for x == 0
  VM_BIT_BSWAP,

  VM_BIT_OP_CNT
} vm_bit_op_e;

/* ==========================================================================
   Fault reasons -- the `reason` field of the two error tags
   ========================================================================== */

/** @brief ERR_VM_EXPR_BAD_CODE: the program is malformed, so the same byte is
 *  wrong on every pass. Reported once per load. */
#define VM_EXPR_BAD_HEADER 0u    // custom_data too small for what the header claims
#define VM_EXPR_BAD_SHAPE 1u     // block declares no output to publish into
#define VM_EXPR_BAD_OPCODE 2u    // no such instruction in this block's palette
#define VM_EXPR_BAD_OPERAND 3u   // operand truncated, or names a pin/literal that isn't there
#define VM_EXPR_BAD_UNDERFLOW 4u // instruction wanted more operands than the stack held
#define VM_EXPR_BAD_OVERFLOW 5u  // expression deeper than VM_EXPR_STACK_MAX
#define VM_EXPR_BAD_RESULT 6u    // stack did not end holding exactly one value

/** @brief ERR_VM_EXPR_MATH: the code is fine and this pass's data is not.
 *  Reported on entering a fault episode, re-armed by a clean evaluation. */
#define VM_EXPR_MATH_DIV0 0u       // divisor, modulus or root degree was zero
#define VM_EXPR_MATH_DOMAIN 1u     // operand outside what the function is defined on
#define VM_EXPR_MATH_NOT_FINITE 2u // result overflowed or came out NaN

// the bodies, so the palette table can name them
void vm_blk_expr(vm_block_h b);
void vm_blk_expr_bit(vm_block_h b);
