from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from dataclasses import dataclass
from typing import Union
from enum import IntEnum
import struct
from GlobalReferences import Global_reference, Global


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

class ForSourceType(IntEnum):
    CONST  = 0  # Wartość stała
    INPUT  = 1  # Wejście z innego bloku
    GLOBAL = 2  # Referencja globalna

# ==========================================
# 2. KONFIGURACJA (PAYLOAD) Z KOMENTARZAMI
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
    what_to_use_mask: int
    
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
        
        # Condition, Operator, Mask (u8 -> 1 bajt każdy)
        hex_cond = val_to_hex(struct.pack('<B', int(self.condition)), 1)
        hex_op   = val_to_hex(struct.pack('<B', int(self.operator)), 1)
        hex_mask = val_to_hex(struct.pack('<B', self.what_to_use_mask), 1)
        
        # Składanie ramki z komentarzami przed każdym polem
        msg_config = (f"# FOR CONFIG # {h2_prefix} {h2_id} "
                      f"# chain # {hex_chain} "
                      f"# cond # {hex_cond} "
                      f"# op # {hex_op} "
                      f"# mask # {hex_mask}")

        return f"{msg_doubles}\n{msg_config}"

# ==========================================
# 3. GŁÓWNA KLASA BLOKU FOR
# ==========================================

class BlockFor(BlockBase):
    def __init__(self, block_idx: int, chain_len: int,
                 start: Union[float, int, str, Global_reference],
                 limit: Union[float, int, str, Global_reference],
                 step: Union[float, int, str, Global_reference],
                 condition: ForCondition,
                 operator: ForOperator):
        
        super().__init__(block_idx, BlockTypes.BLOCK_FOR)
        
        #EN (Enable)
        self.in_data_type_table.append(DataTypes.DATA_B)
        #START 
        self.in_data_type_table.append(DataTypes.DATA_D) 
        #STOP
        self.in_data_type_table.append(DataTypes.DATA_D) 
        #STEP
        self.in_data_type_table.append(DataTypes.DATA_D) 
        
        mask = 0 

        def set_input_mask(param):
            nonlocal mask
            mask <<= 1 
            val_for_hex = 0.0
            if isinstance(param, (int, float)):
                mask |= 1
                val_for_hex = float(param) 
                
            elif isinstance(param, Global_reference):
                self.global_references.append(param)
                val_for_hex = 0.0
                pass
            elif isinstance(param, str):
                pass
            else: 
                raise ValueError(f"BlockFor: Nieobsługiwany typ parametru: {param}")
            return val_for_hex 
                
        #Set 1 if assigned constant value 
        #Assign reference if provided
        stp_val = set_input_mask(step)
        l_val = set_input_mask(limit)
        s_val = set_input_mask(start)
        
        self.config = ForBlockConfig(
            block_id=block_idx,
            chain_len=chain_len,
            start_val=s_val,
            end_val=l_val,
            step_val=stp_val,
            condition=condition,
            operator=operator,
            what_to_use_mask=mask
        )
        
        # Outpun ENO
        self.q_data_type_table.append(DataTypes.DATA_B)
        # Output: Iterator 
        self.q_data_type_table.append(DataTypes.DATA_D)
        
        self.__post_init__()

    def get_extra_data(self) -> str:
        """Zwraca sformatowany string HEX z konfiguracją pętli"""
        return str(self.config)
    0xE

if __name__ == "__main__":
    try:

        blk = BlockFor(
            block_idx=1,
            chain_len=5,
            start=10,
            limit="in",
            step=10,
            condition=ForCondition.LT,
            operator=ForOperator.ADD
        )


        print(blk)
        print(f"Mask Value: 0x{blk.config.what_to_use_mask:02X}")

    except Exception as e:
        print(f"ERROR: {e}")