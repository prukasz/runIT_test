from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from dataclasses import dataclass
from enum import IntEnum
import struct

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
    block_type: BlockTypes = BlockTypes.BLOCK_TIMER
    MSG_TIMER_CONFIG: int = 0x01
    timer_type: TimerType
    default_pt: int  #(ms)

    def __str__(self) -> str:
        # --- HEADER ---
        # [BB] [BlockType] [MsgType]
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, self.block_type, self.MSG_TIMER_CONFIG), 1)
        # [BlockID_L] [BlockID_H]
        h_id     = val_to_hex(struct.pack('<H', self.block_id), 2)

        # --- DATA ---
        # [Type: 1 byte]
        payload_type = val_to_hex(struct.pack('<B', int(self.timer_type)), 1)
        # [DefaultPT: 4 bytes] (uint32)
        payload_pt   = val_to_hex(struct.pack('<I', int(self.default_pt)), 4)

        return f"# TIMER CONFIG # {h_prefix} {h_id} {payload_type} {payload_pt}"

# ==========================================
# BlockTimer
# ==========================================
class BlockTimer(BlockBase):
    def __init__(self, block_idx: int, timer_type: TimerType = TimerType.TON, default_pt: int = 1000):
        super().__init__(block_idx, block_type=BlockTypes.BLOCK_TIMER)

        self.in_data_type_table.append(DataTypes.DATA_B)# [0] EN (Enable)
        self.in_data_type_table.append(DataTypes.DATA_UI32)# [1] PT (Preset Time) 
        self.in_data_type_table.append(DataTypes.DATA_B)# [2] RST (Reset)

        self.q_data_type_table.append(DataTypes.DATA_B)# [0] Q (Output enable) 
        self.q_data_type_table.append(DataTypes.DATA_UI32)# [1] ET (elapsed time)

        self.config = TimerBlockConfig(
            timer_type=timer_type,
            default_pt=default_pt
        )

        self.__post_init__()

    def get_extra_data(self) -> str:
        """Hex string of timer block"""
        return str(self.config)

# ==========================================
# EXAMPLE USAGE
# ==========================================

if __name__ == "__main__":
    try:   
        blk = BlockTimer(
            block_idx=0,
            timer_type=TimerType.TON,
            default_pt=2500
        )

        print(blk)
    except Exception as e:
        print(f"ERROR: {e}")