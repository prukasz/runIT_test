"""
BlockFor - FOR loop block for iterative execution.

Implements a FOR loop with configurable start, limit, step, condition and operator.
"""
import struct
from typing import Optional, Union, List
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


class ForCondition(IntEnum):
    """Loop continuation condition."""
    GT  = 0x01  # > (while iterator > limit)
    LT  = 0x02  # < (while iterator < limit)
    GTE = 0x04  # >= (while iterator >= limit)
    LTE = 0x05  # <= (while iterator <= limit)


class ForOperator(IntEnum):
    """Iterator modification operator."""
    ADD = 0x01  # iterator += step
    SUB = 0x02  # iterator -= step
    MUL = 0x03  # iterator *= step
    DIV = 0x04  # iterator /= step


class BlockFor(Block):
    """
    FOR loop block.
    
    Executes a chain of blocks repeatedly based on loop configuration.
    
    Inputs:
        - in_0 (EN): Enable input (Boolean)
        - in_1 (START): Initial iterator value (optional, overrides config)
        - in_2 (LIMIT): Loop end limit (optional, overrides config)
        - in_3 (STEP): Iterator step value (optional, overrides config)
    
    Outputs:
        - q_0 (ENO): Enable output (Boolean)
        - q_1 (ITER): Current iterator value (Float)
    
    Config:
        - chain_len: Number of blocks in the loop body
        - condition: Loop continuation condition (GT, LT, GTE, LTE)
        - operator: Iterator modification operator (ADD, SUB, MUL, DIV)
        - config_start, config_limit, config_step: Default values
    
    Usage:
        block = BlockFor(
            idx=1,
            ctx=my_context,
            chain_len=3,  # 3 blocks in loop body
            start=0.0,
            limit=10.0,
            step=1.0,
            condition=ForCondition.LT,
            operator=ForOperator.ADD,
            en=Ref("enable")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 chain_len: int,
                 start: Union[float, int, Ref, None] = 0.0,
                 limit: Union[float, int, Ref, None] = 10.0,
                 step: Union[float, int, Ref, None] = 1.0,
                 condition: ForCondition = ForCondition.LT,
                 operator: ForOperator = ForOperator.ADD,
                 en: Optional[Ref] = None):
        """
        Create a FOR loop block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param chain_len: Number of blocks in the loop body
        :param start: Initial iterator value (constant or Ref)
        :param limit: Loop end limit (constant or Ref)
        :param step: Iterator step (constant or Ref)
        :param condition: Loop continuation condition
        :param operator: Iterator modification operator
        :param en: Enable input Ref
        """
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_FOR, ctx=ctx)
        
        self.chain_len = chain_len
        self.condition = condition
        self.operator = operator
        
        self.config_start = 0.0
        self.config_limit = 10.0
        self.config_step = 1.0
        
        # Process parameters
        def process_param(arg, default_val):
            if isinstance(arg, (int, float)):
                return float(arg), None
            elif isinstance(arg, Ref):
                return default_val, arg
            return default_val, None
        
        self.config_start, start_ref = process_param(start, 0.0)
        self.config_limit, limit_ref = process_param(limit, 10.0)
        self.config_step, step_ref = process_param(step, 1.0)
        
        # Inputs: [EN, START, LIMIT, STEP]
        self.add_inputs([en, start_ref, limit_ref, step_ref])
        
        # Outputs
        # q_0: ENO (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
        # q_1: ITER (Float) - current iterator value
        self._add_output(mem_types_t.MEM_F, data=0.0)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack FOR block configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][data...]
        - packet_id 0x00: Float constants [start, limit, step]
        - packet_id 0x10: Config [chain_len:u16, condition:u8, operator:u8]
        """
        packets = []
        
        # Packet 1: Float constants (packet_id = 0x00)
        header1 = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CONSTANTS
        )
        payload1 = struct.pack('<fff', self.config_start, self.config_limit, self.config_step)
        packets.append(header1 + payload1)
        
        # Packet 2: Loop config (packet_id = 0x01)
        header2 = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        payload2 = struct.pack('<HBB', self.chain_len, int(self.condition), int(self.operator))
        packets.append(header2 + payload2)
        
        return packets
    
    def __repr__(self) -> str:
        return (f"BlockFor(idx={self.idx}, chain={self.chain_len}, "
                f"start={self.config_start}, limit={self.config_limit}, step={self.config_step}, "
                f"cond={self.condition.name}, op={self.operator.name})")


# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from MemAcces import AccessManager
    
    print("=" * 60)
    print("BlockFor Test")
    print("=" * 60)
    
    # 1. Create context
    ctx = mem_context_t(ctx_id=0)
    
    # Add variables
    ctx.add(mem_types_t.MEM_B, alias="enable", data=True)
    ctx.add(mem_types_t.MEM_F, alias="dyn_limit", data=20.0)
    
    # 2. Register context
    AccessManager.reset()
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    # 3. Create block context
    block_ctx = mem_context_t(ctx_id=1)
    manager.register_context(block_ctx)
    
    # 4. Create FOR block
    print("\n--- Create BlockFor ---")
    for_block = BlockFor(
        idx=50,
        ctx=block_ctx,
        chain_len=5,  # 5 blocks in loop body
        start=0.0,
        limit=10.0,
        step=1.0,
        condition=ForCondition.LT,
        operator=ForOperator.ADD,
        en=Ref("enable")
    )
    
    print(f"Block: {for_block}")
    
    # 5. Pack all packets
    print("\n--- Pack Configuration ---")
    cfg_bytes = for_block.pack_cfg()
    print(f"Config ({len(cfg_bytes)} bytes): {cfg_bytes.hex().upper()}")
    
    print("\n--- Pack Inputs ---")
    for i, pkt in enumerate(for_block.pack_inputs()):
        print(f"  Input {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Outputs ---")
    for i, pkt in enumerate(for_block.pack_outputs()):
        print(f"  Output {i}: {pkt.hex().upper()}")
    
    print("\n--- Pack Data ---")
    for pkt in for_block.pack_data():
        packet_id = pkt[4]
        if packet_id == 0x00:
            print(f"  Constants: {pkt.hex().upper()}")
        elif packet_id == 0x10:
            print(f"  Config: {pkt.hex().upper()}")
    
    # 6. Create FOR with dynamic limit
    print("\n--- Create BlockFor (dynamic limit) ---")
    for_block2 = BlockFor(
        idx=51,
        ctx=block_ctx,
        chain_len=3,
        start=0.0,
        limit=Ref("dyn_limit"),  # Dynamic from variable
        step=2.0,
        condition=ForCondition.LTE,
        operator=ForOperator.ADD,
        en=Ref("enable")
    )
    
    print(f"Block: {for_block2}")
    
    print("\n" + "=" * 60)
