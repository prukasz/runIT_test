"""
BlockClock - PWM/Clock generator block.

Generates a periodic square wave output with configurable period and pulse width.
"""
import struct
from typing import Optional, Union, List

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


class BlockClock(Block):
    """
    Clock/PWM generator block.
    
    Generates a periodic square wave output. Runs continuously when enabled.
    
    Inputs:
        - in_0 (EN): Enable input (Boolean)
        - in_1 (PERIOD): Period in ms (optional, overrides config)
        - in_2 (WIDTH): Pulse width in ms (optional, overrides config)
    
    Outputs:
        - q_0 (Q): Clock output (Boolean)
    
    Config:
        - config_period: Default period in ms
        - config_width: Default pulse width in ms
    
    Usage:
        block = BlockClock(
            idx=1,
            ctx=my_context,
            period_ms=1000,  # 1 second period
            width_ms=500,    # 50% duty cycle
            en=Ref("enable_signal")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 period_ms: Union[float, int, Ref, None] = 1000.0,
                 width_ms: Union[float, int, Ref, None] = 500.0,
                 en: Optional[Ref] = None):
        """
        Create a Clock/PWM block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param period_ms: Period in ms (constant or Ref)
        :param width_ms: Pulse width in ms (constant or Ref)
        :param en: Enable input Ref
        """
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_CLOCK, ctx=ctx)
        
        self.config_period = 1000
        self.config_width = 500
        
        # Process parameters
        def process_param(arg, default_val):
            if isinstance(arg, (int, float)):
                return int(arg), None
            elif isinstance(arg, Ref):
                return default_val, arg
            return default_val, None
        
        self.config_period, period_ref = process_param(period_ms, 1000)
        self.config_width, width_ref = process_param(width_ms, 500)
        
        # Inputs: [EN, PERIOD, WIDTH]
        self.add_inputs([en, period_ref, width_ref])
        
        # Output: Q (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack clock configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][period:u32][width:u32]
        """
        packets = []
        
        # Config packet (packet_id = 0x01: clock config)
        header = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        # Pack as uint32_t (4 bytes each)
        payload = struct.pack('<II', self.config_period, self.config_width)
        packets.append(header + payload)
        
        return packets
    
    def __repr__(self) -> str:
        duty = (self.config_width / self.config_period * 100) if self.config_period > 0 else 0
        return (f"BlockClock(idx={self.idx}, period={self.config_period}ms, "
                f"width={self.config_width}ms, duty={duty:.1f}%)")


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from MemAcces import AccessManager
    
    print("=" * 60)
    print("BlockClock Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_B, alias="enable", data=True)
    ctx.add(mem_types_t.MEM_F, alias="dyn_period", data=2000.0)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create Clock block with constant values
    print("\n--- Create BlockClock (constant config) ---")
    clock1 = BlockClock(
        idx=30,
        ctx=block_ctx,
        period_ms=1000,
        width_ms=250,  # 25% duty cycle
        en=Ref("enable")
    )
    
    print(f"Block: {clock1}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = clock1.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(clock1.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(clock1.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Data ---")
    for pkt in clock1.pack_data():
        print(f"  Config: {pkt.hex().upper()}")
    
    # 6. Create Clock with dynamic period
    print("\n--- Create BlockClock (dynamic period) ---")
    clock2 = BlockClock(
        idx=31,
        ctx=block_ctx,
        period_ms=Ref("dyn_period"),  # Dynamic
        width_ms=500,
        en=Ref("enable")
    )
    
    print(f"Block: {clock2}")
    
    print("\n" + "=" * 60)
