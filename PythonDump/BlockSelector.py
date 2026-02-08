"""
BlockSelector - Dynamic multiplexer block.

Selects one of multiple input references based on a selector value.
"""
import struct
from typing import List

from Block import Block
from Enums import block_types_t, mem_types_t
from MemAcces import Ref
from Mem import mem_context_t


class BlockSelector(Block):
    """
    Selector/Multiplexer block.
    
    Dynamically switches output to one of multiple input references
    based on a selector value. At runtime, the C code overwrites the
    output instance pointer based on selector input.
    
    Inputs:
        - in_0 (SEL): Selector value (U8) - index of option to select
        - in_1..in_N: Option references (what to select from)
        
    Outputs:
        - q_0: Dummy output (actual output pointer is switched at runtime)
    
    
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
    
        if len(options) == 0:
            raise ValueError(f"BlockSelector idx={idx}: Options must be a non-empty list")
        
        if len(options) > 16:
            raise ValueError(f"BlockSelector idx={idx}: Maximum 16 options supported, got {len(options)}")
        
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_SELECTOR, ctx=ctx)
        
        self.options = options
        self.option_count = len(options)
        
        self.add_inputs([selector]+options)  # in_0 = selector, in_1..in_N = options
        
        # Dummy output (will be overwritten at runtime by C code)
        # Using MEM_B as placeholder - actual type depends on selected option
        self._add_output(mem_types_t.MEM_B, data=False)
    
    #no custom data packets needed for this block, as the C code handles the pointer switching based on selector input
    def pack_data(self) -> List[bytes]:
        return[]