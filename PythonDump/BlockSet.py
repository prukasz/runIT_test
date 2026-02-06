"""
BlockSet - Assigns a value to a target variable.

This is a simple assignment block that copies the value from one 
memory location to another at runtime.
"""
import struct
from typing import Optional
from dataclasses import dataclass

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t
from MemAcces import Ref
from Mem import mem_context_t

class BlockSet(Block):
    """
    SET block - Assigns value to target variable.
    
    Inputs:
        - in_0: Target reference (where to write)
        - in_1: Source value reference (what to write)
    
    Outputs:
        - q_0 (ENO): Enable output (Boolean) - always True after execution
    
    Usage:
        block = BlockSet(
            idx=1,
            ctx=my_context,
            target=Ref("motor_speed"),
            value=Ref("calculated_speed")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 target: Ref,
                 value: Ref):
        """
        Create a SET block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param target: Ref to target variable (where to write)
        :param value: Ref to source value (what to write)
        """
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_SET, ctx=ctx)
        
        # Add inputs: [target, value]
        self.add_inputs([value, target])
        

    def pack_data(self):
        """SET block has no custom data packets."""
        return []
    
    def __repr__(self) -> str:
        return f"BlockSet(idx={self.idx}, in={len(self.in_conn)}, q={len(self.q_conn)})"


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from MemAcces import AccessManager
    
    print("=" * 60)
    print("BlockSet Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_F, alias="motor_speed", data=0.0)
    ctx.add(mem_types_t.MEM_F, alias="calculated_speed", data=100.0)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create SET block
    print("\n--- Create BlockSet ---")
    block = BlockSet(
        idx=5,
        ctx=block_ctx,
        target=Ref("motor_speed"),
        value=Ref("calculated_speed")
    )
    
    print(f"Block: {block}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = block.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(block.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(block.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n" + "=" * 60)

