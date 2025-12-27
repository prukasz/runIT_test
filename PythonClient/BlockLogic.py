import struct
import re
from typing import List, Dict, Union
from dataclasses import dataclass

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
    NOT     = 0x22      # ! (NOWE - Zostawione)

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
        # Regex zaktualizowany: usunięto & i | z klasy znaków pojedynczych
        # Łapie: 
        # 1. Zmienne (in_d)
        # 2. Operatory złożone (&&, ||, ==, >=, <=)
        # 3. Operatory pojedyncze (!, >, <, (, ))
        # 4. Liczby
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
            '!': 4  # Najwyższy priorytet
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
            
            # Liczba lub zmienna
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
            
            # Lewy nawias
            elif token == '(':
                operator_stack.append(token)
            
            # Prawy nawias
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
            return int(match.group(1)) 
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
            
            # Operatory porównania
            elif token == '>':
                instructions.append(Instruction(OpCode.GT, 0x00))
            elif token == '<':
                instructions.append(Instruction(OpCode.LT, 0x00))
            elif token == '==':
                instructions.append(Instruction(OpCode.EQ, 0x00))
            elif token == '>=':
                instructions.append(Instruction(OpCode.GTE, 0x00))
            elif token == '<=':
                instructions.append(Instruction(OpCode.LTE, 0x00))
                
            # Operatory logiczne
            elif token == '&&':
                instructions.append(Instruction(OpCode.AND, 0x00))
            elif token == '||':
                instructions.append(Instruction(OpCode.OR, 0x00))
            elif token == '!':
                instructions.append(Instruction(OpCode.NOT, 0x00))
                
            else:
                raise ValueError(f"Nieznany token w ONP: {token}")
        
        return instructions
    
    def _parse(self):
        tokens = self._tokenize(self.expression)
        rpn = self._infix_to_rpn(tokens)
        self.instructions = self._rpn_to_instructions(rpn)
    
    # ... metody get_message_constants i get_message_code bez zmian ...
    def get_message_constants(self) -> str:
        if not self.constants: return ""
        header = struct.pack('<BBBH', 0xBB, 0x03, 0x01, self.block_id)
        num_constants = struct.pack('<B', len(self.constants))
        constants_bytes = b''.join([struct.pack('<d', c) for c in self.constants])
        return (header + num_constants + constants_bytes).hex().upper()
    
    def get_message_code(self) -> str:
        header = struct.pack('<BBBH', 0xBB, 0x03, 0x02, self.block_id)
        num_instructions = struct.pack('<B', len(self.instructions))
        instructions_bytes = b''.join([
            struct.pack('<BB', instr.opcode, instr.index) for instr in self.instructions
        ])
        return (header + num_instructions + instructions_bytes).hex().upper()
    
    def __str__(self) -> str:
        messages = []
        const_msg = self.get_message_constants()
        if const_msg: messages.append(const_msg)
        messages.append(self.get_message_code())
        return "\n".join(messages)

# ==========================================
# TEST
# ==========================================
if __name__ == "__main__":
    # Przykład: Negacja zmiennej ORAZ porównanie
    expr_str = "(!in_1) && (in_2 > 10)"
    
    parser = LogicExpression(expr_str, block_id=0x0005)
    
    print(f"Wyrażenie: {expr_str}")
    print(f"ONP:       {parser._infix_to_rpn(parser._tokenize(expr_str))}")
    print(f"Bytecode:  {[str(i) for i in parser.instructions]}")
    print("-" * 30)
    print(parser)