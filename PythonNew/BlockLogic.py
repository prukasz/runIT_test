import struct
import re
from typing import List, Dict, Union, Optional
from dataclasses import dataclass
from enum import IntEnum

# Importy bazowe
from BlockBase import BlockBase, BlockType
from EmulatorMemory import EmulatorMemory, DataTypes
from EmulatorMemoryReferences import Global
from BlocksStorage import BlocksStorage

# =============================================================================
# 1. OPCODES I INSTRUKCJE LOGICZNE
# =============================================================================

class OpCode(IntEnum):
    VAR   = 0x00      # zmienna in_x
    CONST = 0x01      # stała liczbowa (indeks do tabeli)
    
    GT    = 0x10      # >
    LT    = 0x11      # <
    EQ    = 0x12      # ==
    GTE   = 0x13      # >=
    LTE   = 0x14      # <=
    
    AND   = 0x20      # &&
    OR    = 0x21      # ||
    NOT   = 0x22      # !

@dataclass
class Instruction:
    opcode: int
    index: int # Index wejścia (VAR) lub index stałej (CONST)

# =============================================================================
# 2. PARSER WYRAŻEŃ LOGICZNYCH (CMP)
# =============================================================================

class LogicExpression:
    def __init__(self, expression: str, block_id: int):
        self.expression = expression.strip()
        self.block_id = block_id
        
        self.constants: List[float] = [] 
        self.const_map: Dict[float, int] = {} 
        self.instructions: List[Instruction] = []
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:
        # Regex łapie zmienne, operatory i liczby
        pattern = r'(in_\d+|&&|\|\||==|>=|<=|[!><()]|\d+\.?\d*|\.\d+)'
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
            '>': 3, '<': 3, '==': 3, '>=': 3, '<=': 3,
            '!': 4 
        }
        return precedence.get(op, 0)
    
    def _is_operator(self, token: Union[str, float]) -> bool:
        return isinstance(token, str) and token in [
            '&&', '||', '!', 
            '==', '>', '<', '>=', '<='
        ]
    
    def _infix_to_rpn(self, tokens: List[Union[str, float]]) -> List[Union[str, float]]:
        output = []
        stack = []
        
        i = 0
        while i < len(tokens):
            token = tokens[i]
            
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
            
            i += 1
        
        while stack:
            op = stack.pop()
            if op == '(': raise ValueError("Niezbalansowane nawiasy")
            output.append(op)
        
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
            if val == 0:
                raise ValueError(f"Error in Logic expr '{self.expression}': 'in_0' is reserved (EN). Use 'in_1' onwards.")
            return val 
        raise ValueError(f"Nieprawidłowa nazwa zmiennej: {var_name}")
    
    def _rpn_to_instructions(self, rpn: List[Union[str, float]]) -> List[Instruction]:
        instructions = []
        
        for token in rpn:
            # Stała
            if isinstance(token, float):
                const_idx = self._add_constant(token)
                instructions.append(Instruction(OpCode.CONST, const_idx))
            
            # Zmienna
            elif isinstance(token, str) and token.startswith('in_'):
                var_idx = self._get_variable_index(token)
                instructions.append(Instruction(OpCode.VAR, var_idx))
            
            # Operatory
            elif token == '>': instructions.append(Instruction(OpCode.GT, 0))
            elif token == '<': instructions.append(Instruction(OpCode.LT, 0))
            elif token == '==': instructions.append(Instruction(OpCode.EQ, 0))
            elif token == '>=': instructions.append(Instruction(OpCode.GTE, 0))
            elif token == '<=': instructions.append(Instruction(OpCode.LTE, 0))
            elif token == '&&': instructions.append(Instruction(OpCode.AND, 0))
            elif token == '||': instructions.append(Instruction(OpCode.OR, 0))
            elif token == '!':  instructions.append(Instruction(OpCode.NOT, 0))
            else:
                raise ValueError(f"Nieznany token w ONP: {token}")
        
        return instructions
    
    def _parse(self):
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)

# =============================================================================
# 3. GŁÓWNA KLASA BLOKU LOGICZNEGO
# =============================================================================

class BlockLogic(BlockBase):
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 expression: str,
                 connections: List[Union[Global, None]] = None,
                 en: Union[Global, None] = None
                 ):
        
        self.parser = LogicExpression(expression, block_id=block_idx)
        
        if connections is None: connections = []
        processed_inputs = [en] + connections
        
        max_in_idx = 0
        for ins in self.parser.instructions:
            if ins.opcode == OpCode.VAR and ins.index > max_in_idx:
                max_in_idx = ins.index
                
        if len(processed_inputs) <= max_in_idx:
             processed_inputs.extend([None] * (max_in_idx - len(processed_inputs) + 1))

        super().__init__(
            block_idx=block_idx,
            block_type=BlockType.BLOCK_CMP, # Zmieniono nazwę w enum na BLOCK_CMP/LOGIC
            storage=storage,
            inputs=processed_inputs,
            output_defs=[
                (DataTypes.DATA_B, []), # Output 0: ENO
                (DataTypes.DATA_B, [])  # Output 1: Result (Bool)
            ]
        )

    # get_custom_data_packet i __str__ analogiczne do BlockMath (zmień tylko nazwy komentarzy na LOGIC)
    def get_custom_data_packet(self) -> List[bytes]:
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
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        custom_pkts = self.get_custom_data_packet()
        for pkt in custom_pkts:
            subtype = pkt[2] 
            hex_str = pkt.hex().upper()
            if subtype == 0x01: lines.append(f"#ID:{self.block_idx} LOGIC Const Table# {hex_str}")
            elif subtype == 0x02: lines.append(f"#ID:{self.block_idx} LOGIC Instructions# {hex_str}")
        return "\n".join(lines)

# =============================================================================
# TEST
# =============================================================================
if __name__ == "__main__":
    from EmulatorMemory import EmulatorMemory
    
    # Setup testowy
    mem_test = EmulatorMemory(1)
    Global.register_memory(mem_test)
    
    try:
        mem_test.add("SensorA", DataTypes.DATA_F, 15.0)
        mem_test.add("SensorB", DataTypes.DATA_F, 0.0)
        mem_test.recalculate_indices()

        # Wyrażenie: (in_1 > 10) && (in_2 == 0)
        expr_str = "(in_1 > 10) && (in_2 == 0)"
        
        blk = BlockLogic(
            block_idx=5, 
            mem_blocks=mem_test,
            expression=expr_str,
            inputs_refs=[Global("SensorA"), Global("SensorB")]
        )
        
        print("--- TEST BLOCK LOGIC ---")
        print(blk)
      
    except Exception as e:
        print(f"ERROR: {e}")