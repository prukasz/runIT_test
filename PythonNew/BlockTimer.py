import struct
from typing import Union, List
from enum import IntEnum

from BlockBase import BlockBase
from EmulatorMemory import EmulatorMemory
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage # Do type hintingu
from Enums import emu_types_t, block_type_t, emu_block_header_t


class TimerType(IntEnum):
    TON = 0x01 # Turn On Delay
    TOF = 0x02 # Turn Off Delay
    TP  = 0x03 # Pulse
    TON_INV = 0x04
    TOF_INV = 0x05
    TP_INV  = 0x06

class BlockTimer(BlockBase):
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 timer_type: TimerType = TimerType.TON, 
                 pt: Union[int, float, Ref, None] = 1000,
                 en: Union[Ref, None] = None,
                 rst: Union[Ref, None] = None):
        
        self.config_pt = 0
        self.timer_type = timer_type
        
        # [0] EN (Enable)
        # [1] PT (Preset Time)
        # [2] RST (Reset)
        processed_inputs = [None] * 3 
        processed_inputs[0] = en
        processed_inputs[2] = rst

        if isinstance(pt, (int, float)):
            self.config_pt = int(pt)
            processed_inputs[1] = None 
        elif isinstance(pt, Ref):
            self.config_pt = 0 
            processed_inputs[1] = pt
        else:
            self.config_pt = 0
            processed_inputs[1] = None

        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_TIMER,
            storage=storage,
            inputs=processed_inputs,
            output_defs=[
                (emu_types_t.DATA_B, []),    # Output 0: Q (Bool)
                (emu_types_t.DATA_UI32, [])  # Output 1: ET (Elapsed Time ms)
            ]
        )

    def get_custom_data_packet(self) -> List[bytes]:
        """
        Format C: [BB] [Type] [01] [ID_L] [ID_H] [TimerType:1] [DefaultPT:4]
        """
        packets = []
        
        # Header: BB BlockType Subtype(01) BlockID
        header = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CONST.value)
        
        # Payload:TimerType(1B) + cfg PT
        payload = struct.pack('<BI', int(self.timer_type), self.config_pt)
        
        packets.append(header + payload)
        return packets

    def __str__(self):
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        
        custom_pkts = self.get_custom_data_packet()
        if custom_pkts:
            hex_str = custom_pkts[0].hex().upper()
            lines.append(f"#ID:{self.block_idx} TIMER Config (Type/PT)# {hex_str}")
        return "\n".join(lines)

# ==========================================
# TEST SAMODZIELNY
# ==========================================
if __name__ == "__main__":
    from EmulatorMemory import EmulatorMemory
    
    # Setup testowy
    mem_blk = EmulatorMemory(1)
    Ref.register_memory(mem_blk)
    storage = BlocksStorage(mem_blk)
    
    try:
        # Przykład 1: Timer ze stałym czasem (TON 5000ms)
        blk_ton = BlockTimer(
            block_idx=10,
            storage=storage,
            timer_type=TimerType.TON,
            pt=5000,
            en=Ref("StartButton") # Zakładamy, że ta zmienna istnieje
        )
        
        # Przykład 2: Timer z czasem ze zmiennej (TOF)
        blk_tof = BlockTimer(
            block_idx=11,
            storage=storage,
            timer_type=TimerType.TOF,
            pt=Ref("DelaySettings"),
            en=blk_ton.out[0], # Kaskada
            rst=Ref("ResetCmd")
        )
        
        mem_blk.recalculate_indices()
        
        print("--- TEST BLOCK TIMER ---")
        print(blk_ton)
        print(blk_tof)
        
    except Exception as e:
        print(f"Error: {e}")