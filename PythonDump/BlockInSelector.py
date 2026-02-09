"""
BlockInSelector - Dynamic multiplexer block (INPUT selector).

Selects one of multiple input references based on a selector value.
"""
import struct
from typing import List

from Block import Block
from Enums import block_types_t, mem_types_t
from MemAcces import Ref
from Mem import mem_context_t


class BlockInSelector(Block):
    """
    IN_SELECTOR: Multiplexer block - selects ONE input to pass through.
    
    Dynamically switches output to one of multiple input references
    based on a selector value. At runtime, the C code overwrites the
    output instance pointer based on selector input.
    
    Inputs:
        - in_0 (EN): Enable signal - block only executes when EN is true
        - in_1 (SEL): Selector value (U8) - index of option to select
        - in_2..in_N+1: Option references (what to select from)
        
    Outputs:
        - q_0: Dummy output (actual output pointer is switched at runtime)
    
    
    Usage:
        block = BlockInSelector(
            idx=1,
            ctx=my_context,
            selector=Ref("mode"),
            options=[Ref("speed_low"), Ref("speed_med"), Ref("speed_high")],
            en=Ref("enable")
        )
        # When mode=0 -> uses speed_low
        # When mode=1 -> uses speed_med
        # When mode=2 -> uses speed_high
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 selector: Ref,
                 options: List[Ref],
                 en: Ref = None):
    
        if len(options) == 0:
            raise ValueError(f"BlockInSelector idx={idx}: Options must be a non-empty list")
        
        if len(options) > 16:
            raise ValueError(f"BlockInSelector idx={idx}: Maximum 16 options supported, got {len(options)}")
        
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_IN_SELECTOR, ctx=ctx)
        
        self.options = options
        self.option_count = len(options)
        
        # in_0 = EN, in_1 = selector, in_2..in_N+1 = options
        self.add_inputs([en, selector]+options)
        
        # Dummy output (will be overwritten at runtime by C code)
        # Using MEM_B as placeholder - actual type depends on selected option
        self._add_output(mem_types_t.MEM_B, data=False)
    
    #no custom data packets needed for this block, as the C code handles the pointer switching based on selector input
    def pack_data(self) -> List[bytes]:
        return[]