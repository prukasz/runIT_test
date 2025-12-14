import struct
import re
from typing import List, Dict, Union
from dataclasses import dataclass

# ==========================================
# TOKENIZACJA
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
# PARSER WYRAŻEŃ
# ==========================================

class MathExpression:

    def __init__(self, expression: str, block_id: int):

        self.expression = expression.strip()
        self.block_id = block_id
        
        # Przetwarzanie
        self.constants: List[float] = []  # Unikalne stałe
        self.const_map: Dict[float, int] = {}  # Mapa: wartość -> index
        self.instructions: List[Instruction] = []  # Instrukcje ONP
        
        self._parse()
    
    def _tokenize(self, expr: str) -> List[Union[str, float]]:

        expr = expr.replace(" ", "")
        
        tokens = []
        i = 0
        
        while i < len(expr):
            # Sprawdzamy funkcje
            if expr[i:i+4] == 'sqrt':
                tokens.append('sqrt')
                i += 4
            elif expr[i:i+3] == 'sin':
                tokens.append('sin')
                i += 3
            elif expr[i:i+3] == 'cos':
                tokens.append('cos')
                i += 3
            elif expr[i:i+3] == 'pow':
                tokens.append('pow')
                i += 3
            elif expr[i:i+3] == 'in_':
                match = re.match(r'in_(\d+)', expr[i:])
                if match:
                    tokens.append(f'in_{match.group(1)}')
                    i += len(match.group(0))
                else:
                    raise ValueError(f"Nieprawidłowy format zmiennej w pozycji {i}")
            # Sprawdzamy liczby (int lub float)
            elif expr[i].isdigit() or (expr[i] == '.' and i+1 < len(expr) and expr[i+1].isdigit()):
                match = re.match(r'(\d+\.?\d*|\.\d+)', expr[i:])
                if match:
                    tokens.append(float(match.group(0)))
                    i += len(match.group(0))
                else:
                    i += 1
            # Operatory i nawiasy
            elif expr[i] in '+-*/()^,':
                tokens.append(expr[i])
                i += 1
            else:
                raise ValueError(f"Nieznany token: '{expr[i]}' w pozycji {i}")
        
        return tokens
    
    def _get_precedence(self, op: str) -> int:
        precedence = {
            '+': 1,
            '-': 1,
            '*': 2,
            '/': 2,
            '^': 3,
            'sqrt': 4,
            'sin': 4,
            'cos': 4,
            'pow': 4,
        }
        return precedence.get(op, 0)
    
    def _is_operator(self, token: Union[str, float]) -> bool:
        return isinstance(token, str) and token in ['+', '-', '*', '/', '^', 'sqrt', 'sin', 'cos', 'pow']
    
    def _is_function(self, token: Union[str, float]) -> bool:
        return isinstance(token, str) and token in ['sqrt', 'sin', 'cos', 'pow']
    
    def _infix_to_rpn(self, tokens: List[Union[str, float]]) -> List[Union[str, float]]:
        output = []
        operator_stack = []
        
        i = 0
        while i < len(tokens):
            token = tokens[i]
            
            # Liczba lub zmienna
            if isinstance(token, float) or (isinstance(token, str) and token.startswith('in_')):
                output.append(token)
            
            # Funkcja
            elif self._is_function(token):
                operator_stack.append(token)
            
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
                # Sprawdź czy przed nawiasem była funkcja
                if operator_stack and self._is_function(operator_stack[-1]):
                    output.append(operator_stack.pop())
            
            # Przecinek (separator argumentów funkcji)
            elif token == ',':
                while operator_stack and operator_stack[-1] != '(':
                    output.append(operator_stack.pop())
            
            i += 1
        
        # Przenosimy pozostałe operatory na wyjście
        while operator_stack:
            output.append(operator_stack.pop())
        
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
            return int(match.group(1))-1
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
            elif token == '+':
                instructions.append(Instruction(OpCode.ADD, 0x00))
            elif token == '-':
                instructions.append(Instruction(OpCode.SUB, 0x00))
            elif token == '*':
                instructions.append(Instruction(OpCode.MUL, 0x00))
            elif token == '/':
                instructions.append(Instruction(OpCode.DIV, 0x00))
            elif token == '^':
                instructions.append(Instruction(OpCode.POW, 0x00))
            elif token == 'sqrt':
                instructions.append(Instruction(OpCode.SQRT, 0x00))
            elif token == 'sin':
                instructions.append(Instruction(OpCode.SIN, 0x00))
            elif token == 'cos':
                instructions.append(Instruction(OpCode.COS, 0x00))
            elif token == 'pow':
                instructions.append(Instruction(OpCode.POW, 0x00))
            else:
                raise ValueError(f"Nieznany token w ONP: {token}")
        
        return instructions
    
    def _parse(self):
        # 1. Tokenizacja
        tokens = self._tokenize(self.expression)
        
        # 2. Konwersja do ONP
        rpn = self._infix_to_rpn(tokens)
        
        # 3. Generowanie instrukcji
        self.instructions = self._rpn_to_instructions(rpn)
    
    def get_message_01(self) -> str:
        header = struct.pack('<BBBH', 0xBB, 0x01, 0x01, self.block_id)
        num_constants = struct.pack('<B', len(self.constants))
        
        # Pakujemy stałe jako double (8 bajtów każda)
        constants_bytes = b''.join([struct.pack('<d', c) for c in self.constants])
        
        return (header + num_constants + constants_bytes).hex().upper()
    
    def get_message_02(self) -> str:
        header = struct.pack('<BBBH', 0xBB, 0x01, 0x02, self.block_id)
        num_instructions = struct.pack('<B', len(self.instructions))
        
        # Pakujemy instrukcje
        instructions_bytes = b''.join([
            struct.pack('<BB', instr.opcode, instr.index) 
            for instr in self.instructions
        ])
        
        return (header + num_instructions + instructions_bytes).hex().upper()
    
    def __str__(self) -> str:

        messages = []
        
        # Dodajemy wiadomość 01 tylko jeśli są stałe
        if self.constants:
            messages.append(self.get_message_01())
        
        # Zawsze dodajemy wiadomość 02 (instrukcje)
        messages.append(self.get_message_02())
        
        return "\n".join(messages)


# ==========================================
# PRZYKŁAD UŻYCIA
# ==========================================

if __name__ == "__main__":
    expr = MathExpression("in_2+12.69", block_id=0x0000)
    print(expr)