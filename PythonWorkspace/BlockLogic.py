import struct
import re
from typing import List, Dict, Union, Optional
from dataclasses import dataclass
from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from GlobalReferences import Global_reference

# ==========================================
# OPCODES I INSTRUKCJE LOGICZNE
# ==========================================

class OpCode:
    VAR     = 0x00      # zmienna in_x
    CONST   = 0x09      # stała liczbowa
    
    GT      = 0x10      # >
    LT      = 0x11      # <
    EQ      = 0x12      # ==
    GTE     = 0x13      # >=
    LTE     = 0x14      # <=
    
    AND     = 0x20      # &&
    OR      = 0x21      # ||
    NOT     = 0x22      # !

@dataclass
class Instruction:
    opcode: int
    index: int
    
    def __str__(self) -> str:
        return f"{self.opcode:02X} {self.index:02X}"


# ==========================================
# PARSER WYRAŻEŃ LOGICZNYCH (CMP)
# ==========================================

class LogicExpression:

    def __init__(self, expression: str, block_id: int):
        self.expression = expression.strip()
        self.block_id = block_id
        
        # Przetwarzanie
        self.constants: List[float] = [] 
        self.const_map: Dict[float, int] = {} 
        self.instructions: List[Instruction] = []
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:
        # Regex łapie zmienne, operatory logiczne i liczby
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
        operator_stack = []
        
        i = 0
        while i < len(tokens):
            token = tokens[i]
            
            if isinstance(token, float) or (isinstance(token, str) and token.startswith('in_')):
                output.append(token)
            
            # Operator
            elif self._is_operator(token):
                while (operator_stack and 
                       operator_stack[-1] != '(' and
                       self._is_operator(operator_stack[-1]) and
                       self._get_precedence(operator_stack[-1]) >= self._get_precedence(token)):
                    output.append(operator_stack.pop())
                operator_stack.append(token)
            
            elif token == '(':
                operator_stack.append(token)

            elif token == ')':
                while operator_stack and operator_stack[-1] != '(':
                    output.append(operator_stack.pop())
                if operator_stack and operator_stack[-1] == '(':
                    operator_stack.pop()
            
            i += 1
        
        while operator_stack:
            op = operator_stack.pop()
            if op == '(':
                raise ValueError("Niezbalansowane nawiasy w wyrażeniu")
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
            # Stała numeryczna
            if isinstance(token, float):
                const_idx = self._add_constant(token)
                instructions.append(Instruction(OpCode.CONST, const_idx))
            
            # Zmienna
            elif isinstance(token, str) and token.startswith('in_'):
                var_idx = self._get_variable_index(token)
                instructions.append(Instruction(OpCode.VAR, var_idx))
            
            # Operatory
            elif token == '>': instructions.append(Instruction(OpCode.GT, 0x00))
            elif token == '<': instructions.append(Instruction(OpCode.LT, 0x00))
            elif token == '==': instructions.append(Instruction(OpCode.EQ, 0x00))
            elif token == '>=': instructions.append(Instruction(OpCode.GTE, 0x00))
            elif token == '<=': instructions.append(Instruction(OpCode.LTE, 0x00))
            elif token == '&&': instructions.append(Instruction(OpCode.AND, 0x00))
            elif token == '||': instructions.append(Instruction(OpCode.OR, 0x00))
            elif token == '!':  instructions.append(Instruction(OpCode.NOT, 0x00))
            else:
                raise ValueError(f"Nieznany token w ONP: {token}")
        
        return instructions
    
    def _parse(self):
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)
    
    def get_message_constants(self) -> str:
        """Message Type 0x01: Constants Table (Doubles grouped by 8 bytes)"""
        if not self.constants: return ""
        
        # Header: BB Type(CMP) 01 BlockID
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, BlockTypes.BLOCK_CMP, 0x01), 1)
        h_id = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Count
        num = val_to_hex(struct.pack('<B', len(self.constants)), 1)
        
        # Doubles lits
        data_parts = []
        for c in self.constants:
            d_bytes = struct.pack('<d', c)
            data_parts.append(val_to_hex(d_bytes, 8))
            
        data_hex = " ".join(data_parts)
        
        #[PREFIX][BB][LOGIC BLOCK][IDX] [HEADER ID](01) [IDX] [cnt] [D1] [D2]...
        return f"#LOGIC CONST# {h_prefix} {h_id} {num} {data_hex}"
    
    def get_message_code(self) -> str:
        """Message Type 0x02: Instructions Code (Grouped by 2 bytes)"""
        
        # Header
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, BlockTypes.BLOCK_CMP, 0x02), 1)
        h_id = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Count
        num = val_to_hex(struct.pack('<B', len(self.instructions)), 1)
        
        # Instructions
        data_parts = []
        for i in self.instructions:
            i_bytes = struct.pack('<BB', i.opcode, i.index)
            data_parts.append(val_to_hex(i_bytes, 2))
            
        data_hex = " ".join(data_parts)

        #[PREFIX][BB][LOGIC BLOCK][IDX] [HEADER ID](02) [IDX] [cnt] [code][idx]  [code][idx]...
        return f"#LOGIC CODE# {h_prefix} {h_id} {num} {data_hex}"
    
    def __str__(self) -> str:
        messages = []
        const_msg = self.get_message_constants()
        if const_msg:
            messages.append(const_msg)
        messages.append(self.get_message_code())
        return "\n".join(messages)



class BlockLogic(BlockBase):
    def __init__(self, block_idx: int, expression: str,
                  in_list: List[DataTypes] = None,
                  global_refs: Optional[Dict[int, Global_reference]] =  None):
        
        super().__init__(block_idx, BlockTypes.BLOCK_CMP)   

        # Input EN (Enable)
        self.in_data_type_table.append(DataTypes.DATA_B)
        
        # Input, User specific
        for input_type in in_list:
            self.in_data_type_table.append(input_type)
        
        if global_refs:
            for inp_idx, g_ref in global_refs.items():
                if inp_idx >= 16:
                    raise ValueError(f"BlockLogic {block_idx}: Global ref idx to high: {inp_idx} (max allowed 16)")
                self.add_global_connection(inp_idx, g_ref)
    
        # Output 0: ENO (Enable Output)
        self.q_data_type_table.append(DataTypes.DATA_B)
        # Output 1: RESULT
        self.q_data_type_table.append(DataTypes.DATA_B)
        self.parser = LogicExpression(expression, block_id=block_idx)

        self.__post_init__()

    def get_extra_data(self) -> str:
        return str(self.parser)


# ==========================================
# TEST
# ==========================================

if __name__ == "__main__":
    try:
   
        expr_str = "(in_1 > 10) && (in_2 == 0)"
        
        blk = BlockLogic(
            block_idx=5, 
            in_list=[DataTypes.DATA_D, DataTypes.DATA_D], 
            expression=expr_str
        )
        
        print(blk)
     
    except Exception as e:
        print(f"ERROR: {e}")