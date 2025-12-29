from BlockBase import BlockBase, val_to_hex
from Enums import DataTypes, BlockTypes
from dataclasses import dataclass
from enum import IntEnum
import struct

# ==========================================
# 1. ENUMY DLA TIMERA
# ==========================================

class TimerType(IntEnum):
    TON = 0x01 # Turn On Delay
    TOF = 0x02 # Turn Off Delay
    TP  = 0x03 # Pulse

# ==========================================
# 2. KONFIGURACJA (PAYLOAD)
# ==========================================

@dataclass
class TimerBlockConfig:
    block_id: int
    timer_type: TimerType
    default_pt: int  # Czas w ms (uint32)

    # ID typu bloku (musi pasować do C, np. 9)
    BLOCK_TIMER_TYPE_ID: int = 0x09 
    MSG_TIMER_CONFIG: int = 0x01

    def __str__(self) -> str:
        # --- HEADER ---
        # [BB] [BlockType] [MsgType]
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, self.BLOCK_TIMER_TYPE_ID, self.MSG_TIMER_CONFIG), 1)
        # [BlockID_L] [BlockID_H]
        h_id     = val_to_hex(struct.pack('<H', self.block_id), 2)

        # --- DATA ---
        # [Type: 1 byte]
        payload_type = val_to_hex(struct.pack('<B', int(self.timer_type)), 1)
        # [DefaultPT: 4 bytes] (uint32)
        payload_pt   = val_to_hex(struct.pack('<I', int(self.default_pt)), 4)

        return f"# TIMER CONFIG # {h_prefix} {h_id} {payload_type} {payload_pt}"

# ==========================================
# 3. GŁÓWNA KLASA BLOKU TIMER
# ==========================================

class BlockTimer(BlockBase):
    def __init__(self, block_idx: int, timer_type: TimerType, default_pt: int = 1000):
        """
        :param block_idx: ID bloku
        :param timer_type: TON / TOF / TP
        :param default_pt: Domyślny czas (ms), używany jeśli wejście PT (Index 1) wisi w powietrzu.
        """
        # Zakładamy, że ID 9 to Timer. Jeśli w Enums.py nie ma BLOCK_TIMER, używamy int(9)
        try:
            b_type = BlockTypes.BLOCK_TIMER
        except AttributeError:
            b_type = BlockTypes(9) # Fallback jeśli enum nie zaktualizowany

        super().__init__(block_idx, b_type)

        # --- INPUTS ---
        # [0] EN (Enable) - BOOL
        self.in_data_type_table.append(DataTypes.DATA_B)
        
        # [1] PT (Preset Time) - DOUBLE
        # Kod C: IN_GET_D(src, 1) -> oczekuje double
        self.in_data_type_table.append(DataTypes.DATA_D)
        
        # [2] RST (Reset) - BOOL
        self.in_data_type_table.append(DataTypes.DATA_B)

        # --- OUTPUTS ---
        # [0] Q (Output) - BOOL
        self.q_data_type_table.append(DataTypes.DATA_B)
        
        # [1] ET (Elapsed Time) - DOUBLE
        # Kod C: utils_set_q_val(..., &elapsed_double) -> zwraca double
        self.q_data_type_table.append(DataTypes.DATA_D)

        # --- CONFIG OBJECT ---
        self.config = TimerBlockConfig(
            block_id=block_idx,
            timer_type=timer_type,
            default_pt=default_pt
        )

        self.__post_init__()

    def get_extra_data(self) -> str:
        """Zwraca sformatowany string HEX z konfiguracją timera"""
        return str(self.config)

# ==========================================
# EXAMPLE USAGE
# ==========================================

if __name__ == "__main__":
    try:
        print("--- Tworzenie Bloku Timer (TON) ---")
        
        # Timer TON, domyślny czas 2500ms (jeśli nie podłączymy wejścia PT)
        blk = BlockTimer(
            block_idx=5,
            timer_type=TimerType.TON,
            default_pt=2500
        )

        print(blk)
        # Oczekiwany wynik HEX configu:
        # BB 09 01 (Header)
        # 05 00    (ID)
        # 01       (Type TON)
        # C4 09 00 00 (2500 DEC = 0x09C4 LE)

    except Exception as e:
        print(f"ERROR: {e}")