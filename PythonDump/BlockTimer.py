
import struct
from typing import Optional, Union, List
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


class TimerType(IntEnum):
    """Timer operation modes."""
    TON     = 0x01  # Turn-On Delay
    TOF     = 0x02  # Turn-Off Delay
    TP      = 0x03  # Pulse
    TON_INV = 0x04  # Turn-On Delay (inverted output)
    TOF_INV = 0x05  # Turn-Off Delay (inverted output)
    TP_INV  = 0x06  # Pulse (inverted output)


class BlockTimer(Block):
    """
    Timer block for time-based control.
    
    Inputs:
        - in_0 (EN): Enable input (Boolean)
        - in_1 (PT): Preset Time in ms (optional, uses config if not connected)
        - in_2 (RST): Reset input (Boolean)
    
    Outputs:
        - q_0 (Q): Timer output (Boolean)
        - q_1 (ET): Elapsed Time in ms (U32)
    
    Config:
        - timer_type: Type of timer (TON, TOF, TP, etc.)
        - config_pt: Preset time in ms (used if PT input is not connected)
    
    Usage:
        block = BlockTimer(
            idx=1,
            ctx=my_context,
            timer_type=TimerType.TON,
            pt=5000,  # 5 seconds, or use Ref for dynamic
            en=Ref("start_signal"),
            rst=Ref("reset_button")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 timer_type: TimerType = TimerType.TON,
                 pt: Union[int, Ref] = 1000,
                 en: Optional[Ref] = None,
                 rst: Optional[Ref] = None):
        
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_TIMER, ctx=ctx)
        
        self.timer_type = timer_type
        self.config_pt = 0
        
        # Process PT input (can be constant or Ref)
        pt_ref = None
        if isinstance(pt, (int, float)):
            self.config_pt = int(pt)
            pt_ref = None
        elif isinstance(pt, Ref):
            self.config_pt = 0  # Will be overwritten by input at runtime
            pt_ref = pt
        
        # Inputs: [EN, PT, RST]
        self.add_inputs([en, pt_ref, rst])
        
        # Outputs
        # q_0: Q (Boolean) - timer output
        self._add_output(mem_types_t.MEM_B, data=False)
        # q_1: ET (U32) - elapsed time in ms
        self._add_output(mem_types_t.MEM_U32, data=0)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack timer configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][timer_type:u8][config_pt:u32]
        """
        packets = []
        
        # Config packet (packet_id = 0x01: timer config)
        header = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        payload = struct.pack('<BI', int(self.timer_type), self.config_pt)
        packets.append(header + payload)
        
        return packets
