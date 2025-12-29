from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from dataclasses import dataclass
from typing import Union
from enum import IntEnum
import struct
from GlobalReferences import Global_reference

# ==========================================
# 1. ENUMY DLA BLOKU FOR
# ==========================================

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

# ==========================================
# 2. KONFIGURACJA (PAYLOAD)
# ==========================================

@dataclass
class ForBlockConfig:
    block_id: int
    chain_len: int
    start_val: float
    end_val: float
    step_val: float
    condition: ForCondition
    operator: ForOperator
    # USUNIĘTO: what_to_use_mask - C robi to teraz automatycznie
    
    BLOCK_FOR_TYPE_ID: int = 0x08 

    def __str__(self) -> str:
        # --- FRAME 1: DOUBLES (Wartości graniczne) ---
        # Header: BB Type 01
        h1_prefix = val_to_hex(struct.pack('<BBB', 0xBB, self.BLOCK_FOR_TYPE_ID, 0x01), 1)
        h1_id     = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Doubles (8 bajtów każdy)
        d_start = val_to_hex(struct.pack('<d', float(self.start_val)), 8)
        d_end   = val_to_hex(struct.pack('<d', float(self.end_val)), 8)
        d_step  = val_to_hex(struct.pack('<d', float(self.step_val)), 8)
        
        msg_doubles = f"# FOR LIMITS # {h1_prefix} {h1_id} {d_start} {d_end} {d_step}"

        # --- FRAME 2: CONFIG (Ustawienia pętli) ---
        # Header: BB Type 02
        h2_prefix = val_to_hex(struct.pack('<BBB', 0xBB, self.BLOCK_FOR_TYPE_ID, 0x02), 1)
        h2_id     = val_to_hex(struct.pack('<H', self.block_id), 2)
        
        # Chain Length (u16 -> 2 bajty)
        hex_chain = val_to_hex(struct.pack('<H', self.chain_len), 2)
        
        # Condition, Operator (u8 -> 1 bajt każdy)
        hex_cond = val_to_hex(struct.pack('<B', int(self.condition)), 1)
        hex_op   = val_to_hex(struct.pack('<B', int(self.operator)), 1)
        
        # USUNIĘTO: hex_mask

        # Składanie ramki
        msg_config = (f"# FOR CONFIG # {h2_prefix} {h2_id} "
                      f"# chain # {hex_chain} "
                      f"# cond # {hex_cond} "
                      f"# op # {hex_op}")

        return f"{msg_doubles}\n{msg_config}"

# ==========================================
# 3. GŁÓWNA KLASA BLOKU FOR
# ==========================================

class BlockFor(BlockBase):
    def __init__(self, block_idx: int, chain_len: int,
                start: Union[float, int, Global_reference] = None,
                 limit: Union[float, int, Global_reference] = None,
                 step: Union[float, int, Global_reference] = None,
                 condition: ForCondition = ForCondition.LT,
                 operator: ForOperator = ForOperator.ADD):
        
        super().__init__(block_idx, BlockTypes.BLOCK_FOR)
        
        self.in_data_type_table.append(DataTypes.DATA_B)# [0] EN (Enable)
        self.in_data_type_table.append(DataTypes.DATA_D)# [1] START  
        self.in_data_type_table.append(DataTypes.DATA_D)# [2] STOP (LIMIT) 
        self.in_data_type_table.append(DataTypes.DATA_D)# [3] STEP 
        
        def process_param(param, input_idx):
            val_for_config = 0.0
            if param is None:
                return val_for_config
            elif isinstance(param, (int, float)):
                val_for_config = float(param)
            elif isinstance(param, Global_reference):       
                self.add_global_connection(input_idx, param)
            else: 
                raise ValueError(f"BlockFor: Nieobsługiwany typ parametru: {param}")
            return val_for_config


        s_val = process_param(start, 1)   # Input 1: Start
        l_val = process_param(limit, 2)   # Input 2: Limit
        stp_val = process_param(step, 3)  # Input 3: Step
        
        # Tworzenie obiektu konfiguracji (bez maski)
        self.config = ForBlockConfig(
            block_id=block_idx,
            chain_len=chain_len,
            start_val=s_val,
            end_val=l_val,
            step_val=stp_val,
            condition=condition,
            operator=operator
        )
        
        self.q_data_type_table.append(DataTypes.DATA_B)# Output [0]: ENO
        self.q_data_type_table.append(DataTypes.DATA_D)# Output [1]: Iterator 
        
        self.__post_init__()

    def get_extra_data(self) -> str:
        """Zwraca sformatowany string HEX z konfiguracją pętli"""
        return str(self.config)

# ==========================================
# TEST
# ==========================================

if __name__ == "__main__":
    try:
        # Przykład użycia
        blk = BlockFor(
            block_idx=1,
            chain_len=5,
            start=10,                      # Stała (trafi do configu)
            limit="in",                    # Wire (trafi jako 0 do configu, ale C użyje kabla)
            step=Global_reference("TEST"), # Global (trafi do listy referencji)
            condition=ForCondition.LT,
            operator=ForOperator.ADD
        )

        print(blk)

    except Exception as e:
        print(f"ERROR: {e}")