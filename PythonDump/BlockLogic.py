"""
BlockLogic - Logic expression evaluator block.

Evaluates boolean expressions with comparison and logical operators.
"""
import struct
import re
from typing import List, Optional, Union, Dict
from dataclasses import dataclass
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


# ============================================================================
# 1. OPCODES - matches C logic_op_code_t
# ============================================================================

class LogicOpCode(IntEnum):
    """Logic block operation codes."""
    VAR   = 0x00  # Variable (from block input)
    CONST = 0x01  # Constant (from constant table)
    
    # Comparison operators
    GT    = 0x10  # >
    LT    = 0x11  # <
    EQ    = 0x12  # ==
    GTE   = 0x13  # >=
    LTE   = 0x14  # <=
    
    # Logical operators
    AND   = 0x20  # &&
    OR    = 0x21  # ||
    NOT   = 0x22  # !


@dataclass
class LogicInstruction:
    """Single logic RPN instruction."""
    opcode: LogicOpCode
    index: int  # Input index (VAR) or constant index (CONST), 0 for operators


# ============================================================================
# 2. LOGIC EXPRESSION PARSER
# ============================================================================

class LogicExpression:
    """
    Parses logic expressions into RPN instructions.
    
    Supports:
    - Variables: in_1, in_2, ... (in_0 is reserved for EN)
    - Constants: numeric values (stored as floats for comparison)
    - Comparison: >, <, ==, >=, <=
    - Logical: &&, ||, !
    
    Example: "(in_1 > 10) && (in_2 < 50)"
    """
    
    def __init__(self, expression: str):
        self.expression = expression.strip()
        self.constants: List[float] = []
        self.const_map: Dict[float, int] = {}
        self.instructions: List[LogicInstruction] = []
        self.max_input_idx: int = 0
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:
        """Tokenize expression."""
        # Pattern matches: variables, operators, numbers
        pattern = r'(in_\d+|&&|\|\||==|>=|<=|!=|[!><()]|\d+\.?\d*|\.\d+)'
        raw_tokens = re.findall(pattern, expr)
        tokens = []
        
        for t in raw_tokens:
            if re.match(r'^\d+\.?\d*$', t) or re.match(r'^\.\d+$', t):
                tokens.append(float(t))
            else:
                tokens.append(t)
        
        return tokens
    
    def _get_precedence(self, op: str) -> int:
        precedence = {
            '||': 1,
            '&&': 2,
            '>': 3, '<': 3, '==': 3, '>=': 3, '<=': 3, '!=': 3,
            '!': 4
        }
        return precedence.get(op, 0)
    
    def _is_operator(self, token) -> bool:
        return isinstance(token, str) and token in [
            '&&', '||', '!',
            '==', '!=', '>', '<', '>=', '<='
        ]
    
    def _infix_to_rpn(self, tokens: List) -> List:
        """Convert infix to RPN using Shunting-yard algorithm."""
        output = []
        stack = []
        
        for token in tokens:
            if isinstance(token, float) or (isinstance(token, str) and token.startswith('in_')):
                output.append(token)
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
        
        while stack:
            op = stack.pop()
            if op != '(':
                output.append(op)
        
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
    
    def _rpn_to_instructions(self, rpn: List) -> List[LogicInstruction]:
        """Convert RPN tokens to instruction list."""
        instructions = []
        
        for token in rpn:
            if isinstance(token, float):
                instructions.append(LogicInstruction(LogicOpCode.CONST, self._add_constant(token)))
            elif isinstance(token, str) and token.startswith('in_'):
                var_idx = self._get_variable_index(token)
                instructions.append(LogicInstruction(LogicOpCode.VAR, var_idx))
            elif token == '>':
                instructions.append(LogicInstruction(LogicOpCode.GT, 0))
            elif token == '<':
                instructions.append(LogicInstruction(LogicOpCode.LT, 0))
            elif token == '==':
                instructions.append(LogicInstruction(LogicOpCode.EQ, 0))
            elif token == '>=':
                instructions.append(LogicInstruction(LogicOpCode.GTE, 0))
            elif token == '<=':
                instructions.append(LogicInstruction(LogicOpCode.LTE, 0))
            elif token == '&&':
                instructions.append(LogicInstruction(LogicOpCode.AND, 0))
            elif token == '||':
                instructions.append(LogicInstruction(LogicOpCode.OR, 0))
            elif token == '!':
                instructions.append(LogicInstruction(LogicOpCode.NOT, 0))
        
        return instructions
    
    def _parse(self):
        """Parse the expression."""
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)


# ============================================================================
# 3. LOGIC BLOCK CLASS
# ============================================================================

class BlockLogic(Block):
    """
    Logic expression evaluator block.
    
    Inputs:
        - in_0 (EN): Enable input (optional, Boolean)
        - in_1..N: Variables used in expression
    
    Outputs:
        - q_0 (ENO): Enable output (Boolean)
        - q_1 (RESULT): Expression result (Boolean)
    
    Usage:
        block = BlockLogic(
            idx=1,
            ctx=my_context,
            expression="(in_1 > 50) && (in_2 < 100)",
            connections=[Ref("temperature"), Ref("pressure")],
            en=Ref("enable")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 expression: str,
                 connections: Optional[List[Optional[Ref]]] = None,
                 en: Optional[Ref] = None):
        """
        Create a Logic block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param expression: Logic expression (e.g., "(in_1 > 50) && (in_2 < 100)")
        :param connections: List of Refs for in_1, in_2, etc.
        :param en: Enable input Ref (optional)
        """
        # 1. Parse expression
        self.parser = LogicExpression(expression)
        self.expression = expression
        
        # 2. Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_LOGIC, ctx=ctx)
        
        # 3. Build inputs list: [EN, in_1, in_2, ...]
        if connections is None:
            connections = []
        
        # Pad connections to match max input index
        while len(connections) < self.parser.max_input_idx:
            connections.append(None)
        
        # Input 0 = EN, Input 1..N = connections
        inputs = [en] + connections
        self.add_inputs(inputs)
        
        # 4. Create outputs
        # Output 0: ENO (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
        # Output 1: RESULT (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack logic block data packets.
        
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
    
    def __repr__(self) -> str:
        return (f"BlockLogic(idx={self.idx}, expr='{self.expression}', "
                f"in={len(self.in_conn)}, q={len(self.q_conn)})")


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from MemAcces import AccessManager
    
    print("=" * 60)
    print("BlockLogic Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_F, alias="temperature", data=75.0)
    ctx.add(mem_types_t.MEM_F, alias="pressure", data=30.0)
    ctx.add(mem_types_t.MEM_B, alias="enable", data=True)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create Logic block
    print("\n--- Create BlockLogic ---")
    block = BlockLogic(
        idx=40,
        ctx=block_ctx,
        expression="(in_1 > 50) && (in_2 < 100)",
        connections=[Ref("temperature"), Ref("pressure")],
        en=Ref("enable")
    )
    
    print(f"Block: {block}")
    print(f"\nParsed expression: {block.expression}")
    print(f"Constants: {block.parser.constants}")
    print(f"Instructions: {[(i.opcode.name, i.index) for i in block.parser.instructions]}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = block.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(block.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(block.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Data ---")
    for pkt in block.pack_data():
        packet_id = pkt[4]
        if packet_id == 0x00:
            print(f"  Constants: {pkt.hex().upper()}")
        elif packet_id == 0x10:
            print(f"  Instructions: {pkt.hex().upper()}")
    
    print("\n" + "=" * 60)

