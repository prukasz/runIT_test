"""
BlockTimer - Timer block with various modes (TON, TOF, TP).

Supports Turn-On Delay, Turn-Off Delay, and Pulse timers.
"""
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
        - config_pt: Default preset time if PT input not connected
    
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
                 pt: Union[int, float, Ref, None] = 1000,
                 en: Optional[Ref] = None,
                 rst: Optional[Ref] = None):
        """
        Create a Timer block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param timer_type: Timer mode (TON, TOF, TP, etc.)
        :param pt: Preset time in ms - constant value or Ref for dynamic
        :param en: Enable input Ref
        :param rst: Reset input Ref
        """
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
    
    def __repr__(self) -> str:
        return (f"BlockTimer(idx={self.idx}, type={self.timer_type.name}, "
                f"pt={self.config_pt}ms, in={len(self.in_conn)}, q={len(self.q_conn)})")


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from MemAcces import AccessManager
    
    print("=" * 60)
    print("BlockTimer Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_B, alias="start_btn", data=False)
    ctx.add(mem_types_t.MEM_B, alias="reset_btn", data=False)
    ctx.add(mem_types_t.MEM_U32, alias="delay_time", data=3000)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create Timer block with constant PT
    print("\n--- Create BlockTimer (constant PT) ---")
    timer1 = BlockTimer(
        idx=10,
        ctx=block_ctx,
        timer_type=TimerType.TON,
        pt=5000,  # 5 seconds
        en=Ref("start_btn"),
        rst=Ref("reset_btn")
    )
    
    print(f"Block: {timer1}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = timer1.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(timer1.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(timer1.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Data ---")
    for pkt in timer1.pack_data():
        print(f"  Config: {pkt.hex().upper()}")
    
    # 6. Create Timer with dynamic PT
    print("\n--- Create BlockTimer (dynamic PT) ---")
    timer2 = BlockTimer(
        idx=11,
        ctx=block_ctx,
        timer_type=TimerType.TOF,
        pt=Ref("delay_time"),  # Dynamic from variable
        en=Ref("start_btn")
    )
    
    print(f"Block: {timer2}")
    print(f"  Note: PT connected to 'delay_time' variable")
    
    print("\n" + "=" * 60)

