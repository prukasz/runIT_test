import struct
import re
from typing import List, Optional, Union, Dict
from dataclasses import dataclass
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref, AccessManager
from Mem import mem_context_t

# ============================================================================
# 1. OPCODES - matches C op_code_t
# ============================================================================

class OpCode(IntEnum):
    """Math block operation codes - matches C op_code_t"""
    OP_VAR   = 0x00  # Variable (from block input)
    OP_CONST = 0x01  # Constant (from constant table)
    OP_ADD   = 0x02  # +
    OP_MUL   = 0x03  # *
    OP_DIV   = 0x04  # /
    OP_COS   = 0x05  # cos()
    OP_SIN   = 0x06  # sin()
    OP_POW   = 0x07  # pow()
    OP_ROOT  = 0x08  # sqrt()
    OP_SUB   = 0x09  # -

@dataclass
class Instruction:
    """Single RPN instruction"""
    opcode: OpCode
    index: int  # Input index (VAR) or constant index (CONST), 0 for operators

# ============================================================================
# 2. EXPRESSION PARSER
# ============================================================================

class MathExpression:
    """
    Parses mathematical expressions into RPN (Reverse Polish Notation) instructions.
    
    Supports:
    - Variables: in_1, in_2, ... (in_0 is reserved for EN)
    - Constants: numeric values (stored as floats)
    - Operators: +, -, *, /, ^
    - Functions: sin, cos, sqrt, pow
    """
    
    def __init__(self, expression: str):
        self.expression = expression.strip()
        self.constants: List[float] = []
        self.const_map: Dict[float, int] = {}
        self.instructions: List[Instruction] = []
        self.max_input_idx: int = 0
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:
        """Tokenize expression into operators, functions, variables, and numbers."""
        expr = expr.replace(" ", "")
        tokens = []
        i = 0
        
        while i < len(expr):
            # Functions
            if expr[i:i+4] == 'sqrt':
                tokens.append('sqrt'); i += 4
            elif expr[i:i+3] == 'sin':
                tokens.append('sin'); i += 3
            elif expr[i:i+3] == 'cos':
                tokens.append('cos'); i += 3
            elif expr[i:i+3] == 'pow':
                tokens.append('pow'); i += 3
            # Variables (in_N)
            elif expr[i:i+3] == 'in_':
                match = re.match(r'in_(\d+)', expr[i:])
                if match:
                    tokens.append(f'in_{match.group(1)}')
                    i += len(match.group(0))
                else:
                    raise ValueError(f"Invalid variable format at position {i}")
            # Numbers
            elif expr[i].isdigit() or (expr[i] == '.' and i+1 < len(expr) and expr[i+1].isdigit()):
                match = re.match(r'(\d+\.?\d*|\.\d+)', expr[i:])
                if match:
                    tokens.append(float(match.group(0)))
                    i += len(match.group(0))
                else:
                    i += 1
            # Operators and parentheses
            elif expr[i] in '+-*/()^,':
                tokens.append(expr[i])
                i += 1
            else:
                raise ValueError(f"Unknown token: '{expr[i]}' at position {i}")
        
        return tokens
    
    def _get_precedence(self, op: str) -> int:
        precedence = {'+': 1, '-': 1, '*': 2, '/': 2, '^': 3, 'sqrt': 4, 'sin': 4, 'cos': 4, 'pow': 4}
        return precedence.get(op, 0)
    
    def _is_operator(self, token) -> bool:
        return isinstance(token, str) and token in ['+', '-', '*', '/', '^', 'sqrt', 'sin', 'cos', 'pow']
    
    def _is_function(self, token) -> bool:
        return isinstance(token, str) and token in ['sqrt', 'sin', 'cos', 'pow']
    
    def _infix_to_rpn(self, tokens: List) -> List:
        """Convert infix notation to Reverse Polish Notation using Shunting-yard algorithm."""
        output = []
        stack = []
        
        for token in tokens:
            if isinstance(token, float) or (isinstance(token, str) and token.startswith('in_')):
                output.append(token)
            elif self._is_function(token):
                stack.append(token)
            elif self._is_operator(token):
                while (stack and stack[-1] != '(' and self._is_operator(stack[-1]) and
                       self._get_precedence(stack[-1]) >= self._get_precedence(token)):
                    output.append(stack.pop())
                stack.append(token)
            elif token == '(':
                stack.append(token)
            elif token == ')':
                while stack and stack[-1] != '(':
                    output.append(stack.pop())
                if stack and stack[-1] == '(':
                    stack.pop()
                if stack and self._is_function(stack[-1]):
                    output.append(stack.pop())
            elif token == ',':
                while stack and stack[-1] != '(':
                    output.append(stack.pop())
        
        while stack:
            output.append(stack.pop())
        
        return output
    
    def _add_constant(self, value: float) -> int:
        """Add constant to table, return its index."""
        if value not in self.const_map:
            idx = len(self.constants)
            self.constants.append(value)
            self.const_map[value] = idx
        return self.const_map[value]
    
    def _get_variable_index(self, var_name: str) -> int:
        """Parse variable name to get input index."""
        match = re.match(r'in_(\d+)', var_name)
        if match:
            val = int(match.group(1))
            if val == 0:
                raise ValueError("'in_0' is reserved for EN. Use 'in_1' onwards.")
            if val > self.max_input_idx:
                self.max_input_idx = val
            return val
        raise ValueError(f"Invalid variable name: {var_name}")
    
    def _rpn_to_instructions(self, rpn: List) -> List[Instruction]:
        """Convert RPN tokens to instruction list."""
        instructions = []
        
        for token in rpn:
            if isinstance(token, float):
                instructions.append(Instruction(OpCode.OP_CONST, self._add_constant(token)))
            elif isinstance(token, str) and token.startswith('in_'):
                var_idx = self._get_variable_index(token)
                instructions.append(Instruction(OpCode.OP_VAR, var_idx))
            elif token == '+':
                instructions.append(Instruction(OpCode.OP_ADD, 0))
            elif token == '-':
                instructions.append(Instruction(OpCode.OP_SUB, 0))
            elif token == '*':
                instructions.append(Instruction(OpCode.OP_MUL, 0))
            elif token == '/':
                instructions.append(Instruction(OpCode.OP_DIV, 0))
            elif token in ['^', 'pow']:
                instructions.append(Instruction(OpCode.OP_POW, 0))
            elif token == 'sqrt':
                instructions.append(Instruction(OpCode.OP_ROOT, 0))
            elif token == 'sin':
                instructions.append(Instruction(OpCode.OP_SIN, 0))
            elif token == 'cos':
                instructions.append(Instruction(OpCode.OP_COS, 0))
        
        return instructions
    
    def _parse(self):
        """Parse the expression."""
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)

# ============================================================================
# 3. MATH BLOCK CLASS
# ============================================================================

class BlockMath(Block):
    """
    Math block for evaluating mathematical expressions.
    
    Inputs:
        - in_0 (EN): Enable input 
        - in_1..N: Variables used in expression
        
    Outputs:
        - q_0 (ENO): Enable output (Boolean)
        - q_1 (RESULT): Expression result (Float)
    
    Usage:
        block = BlockMath(
            idx=1,
            ctx=my_context,
            expression="in_1 * 3.14 + in_2",
            connections=[Ref("speed"), Ref("angle")],
            en=Ref("enable")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 expression: str,
                 en: Ref,
                 connections: Optional[List[Optional[Ref]]] = None,
                 ):

        # 1. Parse expression
        self.parser = MathExpression(expression)
        self.expression = expression
        
        # 2. Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_MATH, ctx=ctx)
        
        # 3. Build inputs list: [EN, in_1, in_2, ...]
        if connections is None:
            connections = []
        
        # Pad connections to match max input index from expression
        while len(connections) < self.parser.max_input_idx:
            connections.append(None)
        
        # Input 0 = EN, Input 1..N = connections
        inputs = [en] + connections
        self.add_inputs(inputs)
        
        # Output 0: ENO (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
        # Output 1: RESULT (Float)
        self._add_output(mem_types_t.MEM_F, data=0.0)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack custom data packets for math block.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][data...]
        - packet_id 0x00: Constants (auto-detect type)
        - packet_id 0x10+: Custom data (instructions)
        """
        packets = []
        
        # Constants packet (packet_id = 0x00)
        if self.parser.constants:
            header = struct.pack('<BHBB',
                packet_header_t.PACKET_H_BLOCK_DATA,
                self.idx,
                self.block_type,
                block_packet_id_t.PKT_CONSTANTS
            )
            count = struct.pack('<B', len(self.parser.constants))
            const_data = bytearray()
            for c in self.parser.constants:
                const_data.extend(struct.pack('<f', c))
            packets.append(header + count + bytes(const_data))
        
        # Instructions packet (packet_id = 0x10)
        if self.parser.instructions:
            header = struct.pack('<BHBB',
                packet_header_t.PACKET_H_BLOCK_DATA,
                self.idx,
                self.block_type,
                block_packet_id_t.PKT_INSTRUCTIONS
            )
            count = struct.pack('<B', len(self.parser.instructions))
            instr_data = bytearray()
            for instr in self.parser.instructions:
                instr_data.extend(struct.pack('<BB', instr.opcode, instr.index))
            packets.append(header + count + bytes(instr_data))
        
        return packets