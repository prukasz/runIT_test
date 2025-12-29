import struct
import re
from typing import List, Dict, Union, Optional
from dataclasses import dataclass
from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from GlobalReferences import Global_reference

# ==========================================
# OPCODES I INSTRUKCJE
# ==========================================

class OpCode:
    VAR = 0x00      # zmienna
    ADD = 0x01      # +
    MUL = 0x02      # *
    DIV = 0x03      # /
    SUB = 0x04      # -
    SQRT = 0x05     # sqrt()
    POW = 0x06      # pow() lub **
    SIN = 0x07      # sin()
    COS = 0x08      # cos()
    CONST = 0x09    # stała

@dataclass
class Instruction:
    opcode: int
    index: int
    
    def __str__(self) -> str:
        return f"{self.opcode:02X} {self.index:02X}"

# ==========================================
# PARSER WYRAŻEŃ (LOGIKA)
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
            
            # --- ZABEZPIECZENIE: Index 0 jest dla EN ---
            if val == 0:
                raise ValueError(f"Error in expr '{self.expression}': 'in_0' is reserved (EN). Use 'in_1' onwards.")
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
            elif token == 'sqrt': instructions.append(Instruction(OpCode.SQRT, 0))
            elif token == 'sin': instructions.append(Instruction(OpCode.SIN, 0))
            elif token == 'cos': instructions.append(Instruction(OpCode.COS, 0))
        return instructions
    
    def _parse(self):
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)
    
    def get_message_01(self) -> str:
        """Message Type 0x01: Constants Table (Doubles grouped by 8 bytes)"""
        if not self.constants: return ""
        
        # Header: BB Type(u8) PacketType(u8)
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, BlockTypes.BLOCK_MATH, 0x01), 1)
        h_id = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Count (u8)
        num = val_to_hex(struct.pack('<B', len(self.constants)), 1)
        
        # Doubles list
        data_parts = []
        for c in self.constants:
            d_bytes = struct.pack('<d', c)
            data_parts.append(val_to_hex(d_bytes, 8))
        
        data_hex = " ".join(data_parts)
        
        return f"#MATH CONST# {h_prefix} {h_id} {num} {data_hex}"
    
    def get_message_02(self) -> str:
        """Message Type 0x02: Instructions Code"""
        
        # Header
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, BlockTypes.BLOCK_MATH, 0x02), 1)
        h_id = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Count
        num = val_to_hex(struct.pack('<B', len(self.instructions)), 1)
        
        data_parts = []
        for i in self.instructions:
            i_bytes = struct.pack('<BB', i.opcode, i.index)
            data_parts.append(val_to_hex(i_bytes, 2)) # Grupowanie po 2
        
        data_hex = " ".join(data_parts)
        
        return f"#MATH CODE# {h_prefix} {h_id} {num} {data_hex}"
    
    def __str__(self) -> str:
        msgs = []
        if self.constants:
            msgs.append(self.get_message_01())
        msgs.append(self.get_message_02())
        return "\n".join(msgs)


# ==========================================
# KLASA BLOKU MATH
# ==========================================

class BlockMath(BlockBase):
    def __init__(self, block_idx: int,  expression: str, in_list: List[DataTypes]= None, global_refs: Optional[Dict[int, Global_reference]] = None,):
        
        super().__init__(block_idx, BlockTypes.BLOCK_MATH)

        # Input EN (Enable)
        self.in_data_type_table.append(DataTypes.DATA_B)
        
        # User inputs
        if in_list is None:
            in_list = []
        for input_type in in_list:
            self.in_data_type_table.append(input_type)
    
        # Outputs [0]=ENO, [1]=Result
        self.q_data_type_table.append(DataTypes.DATA_B)
        self.q_data_type_table.append(DataTypes.DATA_D)
        if global_refs:
            for inp_idx, g_ref in global_refs.items():
                if inp_idx >= 16:
                    raise ValueError(f"BlockLogic {block_idx}: Global ref idx to high: {inp_idx} (max allowed 16)")
                self.add_global_connection(inp_idx, g_ref)

        self.parser = MathExpression(expression, block_id=block_idx)

        self.__post_init__()

    def get_extra_data(self) -> str:
        return str(self.parser)


# ==========================================
# TEST
# ==========================================

if __name__ == "__main__":
    try:
        print("--- TEST BlockMath ---")
        expr_str = "in_1 * 3.14 + 10"
        
        # Tworzymy blok z 1 dodatkowym wejściem typu I16 (będzie to Input 1)
        blk = BlockMath(
            block_idx=1, 
            in_list=[DataTypes.DATA_I16], 
            expression=expr_str
        )
        
        print(blk)
     
    except Exception as e:
        print(f"ERROR: {e}")