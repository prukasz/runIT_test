from enum import IntEnum 
from Enums import DataTypes, BlockTypes

class ForCondition(IntEnum):
    GT  = 0x01 # >
    LT  = 0x02 # <
    GTE = 0x04 # >=
    LTE = 0x05 # <=

class ForOperator(IntEnum):
    ADD = 0x01
    SUB = 0x02
    MUL = 0x03
    DIV = 0x04

# Pomocniczy enum do maski (what_to_use)
class ForSourceType(IntEnum):
    CONST  = 0  # Wartość wpisana w konfigurację
    INPUT  = 1  # Wejście zwykłe (block input)
    GLOBAL = 2  # Referencja globalna (w C obsługiwana jako setting == 2)
    # Jeśli Twój kod C wymaga wartości 3 dla globali, zmień powyżej na 3.
    # W poprzednim kodzie C: setting == 2 uruchamiało utils_get_in_val_autoselect
import struct
from typing import Union
from References import Global_reference

# Klasa pomocnicza przechowująca konfigurację specyficzną dla FOR
# Odpowiada za generowanie ramek 0x01 (Doubles) i 0x02 (Config)
@dataclass
class ForBlockConfig:
    block_id: int
    chain_len: int
    start_val: float
    end_val: float
    step_val: float
    condition: ForCondition
    operator: ForOperator
    what_to_use_mask: int
    
    # ID typu bloku FOR (musi pasować do BLOCK_FOR w C, np. 0x08)
    BLOCK_FOR_TYPE_ID: int = 0x08 

    def __str__(self) -> str:
        # --- WIADOMOŚĆ 1: WARTOŚCI DOUBLE (Type 0x01) ---
        # Header: BB [TYPE] 01 [BlockID]
        header_dbl = struct.pack('<BBBH', 0xBB, self.BLOCK_FOR_TYPE_ID, 0x01, self.block_id)
        # Payload: Start(8) + End(8) + Step(8)
        payload_dbl = struct.pack('<ddd', float(self.start_val), float(self.end_val), float(self.step_val))
        msg_doubles = (header_dbl + payload_dbl).hex().upper()

        # --- WIADOMOŚĆ 2: KONFIGURACJA (Type 0x02) ---
        # Header: BB [TYPE] 02 [BlockID]
        header_cfg = struct.pack('<BBBH', 0xBB, self.BLOCK_FOR_TYPE_ID, 0x02, self.block_id)
        # Payload: ChainLen(2) + Cond(1) + Op(1) + Mask(1)
        payload_cfg = struct.pack('<HBBB', 
                                  self.chain_len, 
                                  int(self.condition), 
                                  int(self.operator), 
                                  self.what_to_use_mask)
        msg_config = (header_cfg + payload_cfg).hex().upper()

        # Zwracamy obie ramki oddzielone nową linią (parser BlockHandle to obsłuży)
        return msg_doubles + "\n" + msg_config


# Główna klasa Bloku FOR
@dataclass
class BlockFor(BlockHandle):
    def __init__(self, block_idx: int, chain_len: int,
                 start: Union[float, int, str, Global_reference],
                 limit: Union[float, int, str, Global_reference],
                 step: Union[float, int, str, Global_reference],
                 condition: ForCondition,
                 operator: ForOperator):
        
        # Inicjalizacja podstawowa BlockHandle
        super().__init__(block_idx, BlockTypes.Bl) # Upewnij się, że BlockTypes.BLOCK_FOR istnieje
        
        # 1. Wejście 0: Zawsze EN (Enable) - Bool
        self.in_data_type_table.append(DataTypes.DATA_B)
        self.in_used += 1 # Zakładamy, że EN jest używane

        # Zmienne pomocnicze do budowy maski
        mask = 0
        
        # Funkcja pomocnicza do analizy źródła (Start/Limit/Step)
        # Zwraca: Wartość do wpisania w double (jeśli const) lub 0.0
        # Modyfikuje: self.in_data_type_table, self.global_reference, mask
        def process_param(param, shift_bits):
            nonlocal mask
            val_for_double = 0.0
            
            if isinstance(param, (int, float)):
                # STAŁA (Constant)
                val_for_double = float(param)
                # Mask bits: 0 (00) -> nic nie robimy
                
            elif isinstance(param, str) and param.startswith("in_"):
                # WEJŚCIE ZWYKŁE (Input)
                # Dodajemy wejście typu DOUBLE do listy wejść
                self.in_data_type_table.append(DataTypes.DATA_D)
                # Ustawiamy bity maski na 1 (01)
                mask |= (ForSourceType.INPUT << shift_bits)
                
            elif isinstance(param, Global_reference):
                # REFERENCJA GLOBALNA (Global)
                # Dodajemy do listy referencji
                self.global_reference.append(param)
                # Ustawiamy bity maski na 2 (10) lub 3 (11) zależnie od potrzeb C
                mask |= (ForSourceType.GLOBAL << shift_bits)
                
            else:
                raise ValueError(f"Nieobsługiwany typ parametru w BlockFor: {type(param)}")
                
            return val_for_double

        # 2. Przetwarzanie parametrów i budowa maski
        # Bit 0-1: Start
        s_val = process_param(start, 0)
        # Bit 2-3: Limit
        l_val = process_param(limit, 2)
        # Bit 4-5: Step
        stp_val = process_param(step, 4)

        # 3. Tworzenie obiektu konfiguracyjnego (który wygeneruje HEX w block_data)
        self.block_data = ForBlockConfig(
            block_id=block_idx,
            chain_len=chain_len,
            start_val=s_val,
            end_val=l_val,
            step_val=stp_val,
            condition=condition,
            operator=operator,
            what_to_use_mask=mask
        )
        
        # Aktualizacja liczników (wymagane przez BlockHandle logic)
        self.in_cnt = len(self.in_data_type_table)
        
        # Wyjścia: Block FOR zazwyczaj ma 1 wyjście (Iterator)
        # Możesz tu dodać DataTypes.DOUBLE
        self.q_data_type_table.append(DataTypes.DATA_D)
        self.q_cnt = len(self.q_data_type_table)
        
        # Inicjalizacja pustych połączeń (zgodnie z logiką BlockHandle)
        while len(self.q_connections_table) < self.q_cnt:
            self.q_connections_table.append(QConnection())