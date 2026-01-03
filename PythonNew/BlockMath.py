import struct
import re
from typing import List, Union, Dict
from dataclasses import dataclass
from enum import IntEnum

from BlockBase import BlockBase, BlockType
from EmulatorMemory import DataTypes
from EmulatorMemoryReferences import Global
from BlocksStorage import BlocksStorage # Do type hintingu

# ==========================================
# 1. OPCODES I INSTRUKCJE
# ==========================================

class OpCode(IntEnum):
    VAR   = 0x00      # zmienna (z wejścia bloku)
    CONST = 0x01      # stała (z tabeli stałych)
    ADD   = 0x02      # +
    MUL   = 0x03      # *
    DIV   = 0x04      # /
    COS   = 0x05      # cos()
    SIN   = 0x06      # sin()
    POW   = 0x07      # pow()
    ROOT  = 0x08      # sqrt()
    SUB   = 0x09      # -

@dataclass
class Instruction:
    opcode: int
    index: int # Index wejścia (VAR) lub index stałej (CONST)

# ==========================================
# 2. PARSER WYRAŻEŃ (LOGIKA)
# ==========================================

class MathExpression:
    def __init__(self, expression: str, block_id: int):
        self.expression = expression.strip()
        self.block_id = block_id 
        self.constants: List[float] = [] 
        self.const_map: Dict[float, int] = {} 
        self.instructions: List[Instruction] = []
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:
        expr = expr.replace(" ", "")
        tokens = []
        i = 0
        while i < len(expr):
            if expr[i:i+4] == 'sqrt':
                tokens.append('sqrt'); i += 4
            elif expr[i:i+3] == 'sin':
                tokens.append('sin'); i += 3
            elif expr[i:i+3] == 'cos':
                tokens.append('cos'); i += 3
            elif expr[i:i+3] == 'pow':
                tokens.append('pow'); i += 3
            elif expr[i:i+3] == 'in_':
                match = re.match(r'in_(\d+)', expr[i:])
                if match:
                    tokens.append(f'in_{match.group(1)}')
                    i += len(match.group(0))
                else:
                    raise ValueError(f"Invalid variable format at position {i}")
            elif expr[i].isdigit() or (expr[i] == '.' and i+1 < len(expr) and expr[i+1].isdigit()):
                match = re.match(r'(\d+\.?\d*|\.\d+)', expr[i:])
                if match:
                    tokens.append(float(match.group(0))); i += len(match.group(0))
                else: i += 1
            elif expr[i] in '+-*/()^,':
                tokens.append(expr[i]); i += 1
            else:
                raise ValueError(f"Unknown token: '{expr[i]}' at position {i}")
        return tokens
    
    def _get_precedence(self, op: str) -> int:
        precedence = {'+':1, '-':1, '*':2, '/':2, '^':3, 'sqrt':4, 'sin':4, 'cos':4, 'pow':4}
        return precedence.get(op, 0)
    
    def _is_operator(self, token) -> bool:
        return isinstance(token, str) and token in ['+', '-', '*', '/', '^', 'sqrt', 'sin', 'cos', 'pow']
    
    def _is_function(self, token) -> bool:
        return isinstance(token, str) and token in ['sqrt', 'sin', 'cos', 'pow']
    
    def _infix_to_rpn(self, tokens: List) -> List:
        output = []; stack = []; i = 0
        while i < len(tokens):
            token = tokens[i]
            if isinstance(token, float) or (isinstance(token, str) and token.startswith('in_')):
                output.append(token)
            elif self._is_function(token): stack.append(token)
            elif self._is_operator(token):
                while (stack and stack[-1] != '(' and self._is_operator(stack[-1]) and
                       self._get_precedence(stack[-1]) >= self._get_precedence(token)):
                    output.append(stack.pop())
                stack.append(token)
            elif token == '(': stack.append(token)
            elif token == ')':
                while stack and stack[-1] != '(': output.append(stack.pop())
                if stack and stack[-1] == '(': stack.pop()
                if stack and self._is_function(stack[-1]): output.append(stack.pop())
            elif token == ',':
                while stack and stack[-1] != '(': output.append(stack.pop())
            i += 1
        while stack: output.append(stack.pop())
        return output
    
    def _add_constant(self, value: float) -> int:
        if value not in self.const_map:
            idx = len(self.constants)
            self.constants.append(value)
            self.const_map[value] = idx
        return self.const_map[value]
    
    def _get_variable_index(self, var_name: str) -> int:
        match = re.match(r'in_(\d+)', var_name)
        if match:
            val = int(match.group(1))
            # Input 0 is reserved for EN (Enable)
            if val == 0:
                raise ValueError(f"Error: 'in_0' is reserved (EN). Use 'in_1' onwards.")
            return val 
        raise ValueError(f"Invalid variable name: {var_name}")
    
    def _rpn_to_instructions(self, rpn: List) -> List[Instruction]:
        instructions = []
        for token in rpn:
            if isinstance(token, float):
                instructions.append(Instruction(OpCode.CONST, self._add_constant(token)))
            elif isinstance(token, str) and token.startswith('in_'):
                var_idx = self._get_variable_index(token)
                instructions.append(Instruction(OpCode.VAR, var_idx))
            elif token == '+': instructions.append(Instruction(OpCode.ADD, 0))
            elif token == '-': instructions.append(Instruction(OpCode.SUB, 0))
            elif token == '*': instructions.append(Instruction(OpCode.MUL, 0))
            elif token == '/': instructions.append(Instruction(OpCode.DIV, 0))
            elif token in ['^', 'pow']: instructions.append(Instruction(OpCode.POW, 0))
            elif token == 'sqrt': instructions.append(Instruction(OpCode.ROOT, 0)) # Uwaga na nazwę opcodu
            elif token == 'sin': instructions.append(Instruction(OpCode.SIN, 0))
            elif token == 'cos': instructions.append(Instruction(OpCode.COS, 0))
        return instructions
    
    def _parse(self):
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)

# ==========================================
# 3. GŁÓWNA KLASA BLOKU MATH
# ==========================================

class BlockMath(BlockBase):
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 expression: str,
                 connections: List[Union[Global, None]] = None,
                 en: Union[Global, None] = None
                 ):
        
        # 1. Parsowanie
        self.parser = MathExpression(expression, block_id=block_idx)
        
        # 2. Przygotowanie wejść
        if connections is None: connections = []
            
        # Input 0: EN
        # Input 1..N: Połączenia z connections (mapowane na in_1, in_2...)
        processed_inputs = [en] + connections
        
        # Padding dla brakujących wejść
        max_in_idx = 0
        for ins in self.parser.instructions:
            if ins.opcode == OpCode.VAR and ins.index > max_in_idx:
                max_in_idx = ins.index
                
        if len(processed_inputs) <= max_in_idx:
             processed_inputs.extend([None] * (max_in_idx - len(processed_inputs) + 1))

        super().__init__(
            block_idx=block_idx,
            block_type=BlockType.BLOCK_MATH,
            storage=storage,
            inputs=processed_inputs,
            output_defs=[
                (DataTypes.DATA_B, []), # Output 0: ENO
                (DataTypes.DATA_D, [])  # Output 1: Result
            ]
        )

    def get_custom_data_packet(self) -> List[bytes]:
        # ... (Bez zmian: generowanie pakietów Const i Code) ...
        # (Skopiuj implementację get_custom_data_packet z poprzedniej wersji BlockMath.py)
        # Pamiętaj o imporcie struct
        packets = []
        if self.parser.constants:
            h1 = self._pack_common_header(0x01)
            c1 = struct.pack('B', len(self.parser.constants))
            p1 = bytearray()
            for c in self.parser.constants: p1.extend(struct.pack('<d', c))
            packets.append(h1 + c1 + p1)

        if self.parser.instructions:
            h2 = self._pack_common_header(0x02)
            c2 = struct.pack('B', len(self.parser.instructions))
            p2 = bytearray()
            for i in self.parser.instructions: p2.extend(struct.pack('<BB', i.opcode, i.index))
            packets.append(h2 + c2 + p2)
        return packets

    def __str__(self):
        # ... (Formatowanie z komentarzami - bez zmian) ...
        # Skopiuj __str__ z poprzedniej wersji
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        custom_pkts = self.get_custom_data_packet()
        for pkt in custom_pkts:
            subtype = pkt[2] 
            hex_str = pkt.hex().upper()
            if subtype == 0x01: lines.append(f"#ID:{self.block_idx} MATH Const Table# {hex_str}")
            elif subtype == 0x02: lines.append(f"#ID:{self.block_idx} MATH Instructions# {hex_str}")
        return "\n".join(lines)

# ==========================================
# TEST SAMODZIELNY
# ==========================================
if __name__ == "__main__":
    from EmulatorMemory import EmulatorMemory
    
    mem_test = EmulatorMemory(1)
    Global.register_memory(mem_test)
    
    try:
        # Wyrażenie: (in_1 * 3.14) + in_2
        # Input 1: Zmienna globalna "A" (zakładamy, że istnieje w mem_test symulacyjnie)
        # Input 2: Zmienna globalna "B"
        
        mem_test.add("A", DataTypes.DATA_F, 1.0)
        mem_test.add("B", DataTypes.DATA_F, 2.0)
        mem_test.recalculate_indices()

        blk = BlockMath(
            block_idx=10,
            mem_blocks=mem_test,
            expression="in_1 * 3.14 + in_2",
            en=None,
            inputs_refs=[Global("A"), Global("B")]
        )
        
        print("--- TEST BLOCK MATH ---")
        print(blk)
        
    except Exception as e:
        print(f"Error: {e}")