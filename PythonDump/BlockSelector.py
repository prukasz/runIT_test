"""
BlockSelector - Dynamic multiplexer block.

Selects one of multiple input references based on a selector value.
"""
import struct
from typing import List

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref, AccessManager
from Mem import mem_context_t


class BlockSelector(Block):
    """
    Selector/Multiplexer block.
    
    Dynamically switches output to one of multiple input references
    based on a selector value. At runtime, the C code overwrites the
    output instance pointer based on selector input.
    
    Inputs:
        - in_0 (SEL): Selector value (U8) - index of option to select
    
    Outputs:
        - q_0: Dummy output (actual output pointer is switched at runtime)
    
    Options:
        - List of Refs that can be selected (max 16)
    
    Usage:
        block = BlockSelector(
            idx=1,
            ctx=my_context,
            selector=Ref("mode"),
            options=[Ref("speed_low"), Ref("speed_med"), Ref("speed_high")]
        )
        # When mode=0 -> uses speed_low
        # When mode=1 -> uses speed_med
        # When mode=2 -> uses speed_high
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 selector: Ref,
                 options: List[Ref]):
        """
        Create a Selector block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param selector: Ref to selector variable (should be U8)
        :param options: List of Refs (1-16) to choose from
        :raises ValueError: If selector is not Ref
        :raises ValueError: If options is empty or has more than 16 items
        :raises ValueError: If any option is not a Ref
        """
        if not isinstance(selector, Ref):
            raise ValueError(f"BlockSelector idx={idx}: Selector must be a Ref")
        
        if not isinstance(options, list) or len(options) == 0:
            raise ValueError(f"BlockSelector idx={idx}: Options must be a non-empty list")
        
        if len(options) > 16:
            raise ValueError(f"BlockSelector idx={idx}: Maximum 16 options supported, got {len(options)}")
        
        for i, opt in enumerate(options):
            if not isinstance(opt, Ref):
                raise ValueError(f"BlockSelector idx={idx}: Option {i} must be a Ref")
        
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_SELECTOR, ctx=ctx)
        
        self.options = options
        self.option_count = len(options)
        
        # Input: [selector]
        self.add_inputs([selector])
        
        # Dummy output (will be overwritten at runtime by C code)
        # Using MEM_B as placeholder - actual type depends on selected option
        self._add_output(mem_types_t.MEM_B, data=False)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack selector options as data packets.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][access_packet bytes]
        - packet_id 0x10 + option_index for each option
        """
        packets = []
        manager = AccessManager.get_instance()
        
        for i, option in enumerate(self.options):
            if i >= 16:
                break
            
            # Serialize the option reference to bytes
            option_bytes = option.to_bytes(manager)
            
            # Packet ID: 0x20 + option_index
            header = struct.pack('<BHBB',
                packet_header_t.PACKET_H_BLOCK_DATA,
                self.idx,
                self.block_type,
                block_packet_id_t.PKT_OPTIONS_BASE + i
            )
            packets.append(header + option_bytes)
        
        return packets
    
    def __repr__(self) -> str:
        return (f"BlockSelector(idx={self.idx}, options={self.option_count}, "
                f"in={len(self.in_conn)}, q={len(self.q_conn)})")


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    print("=" * 60)
    print("BlockSelector Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_U8, alias="mode", data=0)
    ctx.add(mem_types_t.MEM_F, alias="speed_low", data=10.0)
    ctx.add(mem_types_t.MEM_F, alias="speed_med", data=50.0)
    ctx.add(mem_types_t.MEM_F, alias="speed_high", data=100.0)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create Selector block
    print("\n--- Create BlockSelector ---")
    selector = BlockSelector(
        idx=60,
        ctx=block_ctx,
        selector=Ref("mode"),
        options=[
            Ref("speed_low"),
            Ref("speed_med"),
            Ref("speed_high")
        ]
    )
    
    print(f"Block: {selector}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = selector.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(selector.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(selector.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Data (Options) ---")
    for i, pkt in enumerate(selector.pack_data()):
        packet_id = pkt[4]
        opt_idx = packet_id - 0x10
        print(f"  Option {opt_idx}: {pkt.hex().upper()}")
    
    print("\n" + "=" * 60)
    print("Selector Usage:")
    print("  When mode=0 -> output points to speed_low")
    print("  When mode=1 -> output points to speed_med")
    print("  When mode=2 -> output points to speed_high")
    print("=" * 60)

