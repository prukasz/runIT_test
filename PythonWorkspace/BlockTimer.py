from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from dataclasses import dataclass
from enum import IntEnum
from typing import Union, Optional
import struct
from GlobalReferences import Global_reference

# ==========================================
# TIMER OPTIONS
# ==========================================

class TimerType(IntEnum):
    TON = 0x01 # Turn On Delay
    TOF = 0x02 # Turn Off Delay
    TP  = 0x03 # Pulse

# ==========================================
# TIMER CONFIG
# ==========================================
@dataclass
class TimerBlockConfig:
    block_id: int
    timer_type: TimerType
    default_pt: int  #(ms)
    
    block_type: BlockTypes = BlockTypes.BLOCK_TIMER
    MSG_TIMER_CONFIG: int = 0x01

    def __str__(self) -> str:
        # --- HEADER ---
        # [BB] [BlockType] [MsgType]
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, int(self.block_type), self.MSG_TIMER_CONFIG), 1)
        # [BlockID_L] [BlockID_H]
        h_id     = val_to_hex(struct.pack('<H', self.block_id), 2)

        # --- DATA ---
        # [Type: 1 byte]
        payload_type = val_to_hex(struct.pack('<B', int(self.timer_type)), 1)
        
        # [DefaultPT: 4 bytes] (uint32) - Little Endian
        # Note: Even though input wire is Double, the internal config default is usually int ms
        payload_pt   = val_to_hex(struct.pack('<I', int(self.default_pt)), 4)

        return f"# TIMER CONFIG # {h_prefix} {h_id} {payload_type} {payload_pt}"

# ==========================================
# BlockTimer
# ==========================================
class BlockTimer(BlockBase):
    def __init__(self, block_idx: int, 
                 timer_type: TimerType = TimerType.TON, 
                 pt: Union[int, float, Global_reference] = 1000,
                 en: Optional[Global_reference] = None,
                 rst: Optional[Global_reference] = None):
        super().__init__(block_idx, block_type=BlockTypes.BLOCK_TIMER)

        # ---------------------------------------------------------------------
        # DEFINITION OF INPUTS (Fixed Layout for C)
        # ---------------------------------------------------------------------
        
        # [0] EN (Enable/Input)
        self.in_data_type_table.append(DataTypes.DATA_B)
        
        # [1] PT (Preset Time)
        # We use DATA_D (Double) to be compatible with Math blocks and Global Vars
        # The C code casts this to uint32_t: (uint32_t)IN_GET_D(...)
        self.in_data_type_table.append(DataTypes.DATA_UI32) 
        
        # [2] RST (Reset)
        self.in_data_type_table.append(DataTypes.DATA_B)

        # ---------------------------------------------------------------------
        # DEFINITION OF OUTPUTS
        # ---------------------------------------------------------------------
        
        # [0] Q (Output Boolean)
        self.q_data_type_table.append(DataTypes.DATA_B)
        
        # [1] ET (Elapsed Time)
        # We export as Double to allow math operations on time
        self.q_data_type_table.append(DataTypes.DATA_UI32)

        # ---------------------------------------------------------------------
        # PARAMETER PROCESSING
        # ---------------------------------------------------------------------
        
        # 1. Handle EN (Input 0)
        if en and isinstance(en, Global_reference):
            self.add_global_connection(0, en)
            
        # 2. Handle RST (Input 2)
        if rst and isinstance(rst, Global_reference):
            self.add_global_connection(2, rst)

        # 3. Handle PT (Input 1) and Config
        config_pt_val = 0
        
        if isinstance(pt, (int, float)):
            # Static value -> Goes to Config
            config_pt_val = int(pt)
        elif isinstance(pt, Global_reference):
            # Global -> Goes to References, Config gets 0 (ignored by C if Global exists)
            self.add_global_connection(1, pt)
            config_pt_val = 0
        else:
            # Wire/None -> Config gets default safe value (e.g. 0), C waits for wire
            config_pt_val = 0

        self.config = TimerBlockConfig(
            block_id=block_idx,
            timer_type=timer_type,
            default_pt=config_pt_val
        )

        self.__post_init__()

    def get_extra_data(self) -> str:
        """Hex string of timer block config"""
        return str(self.config)
