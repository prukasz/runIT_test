"""
BlockQSelector - Output demultiplexer block (Q_SELECTOR).

Produces N boolean outputs where only ONE is true (switch-case behavior).
"""
import struct
from typing import List

from Block import Block
from Enums import block_types_t, mem_types_t
from MemAcces import Ref
from Mem import mem_context_t


class BlockQSelector(Block):
    """
    Q_SELECTOR: Demultiplexer block - activates ONE of N boolean outputs.
    
    Like a switch-case statement: based on selector value, sets one output
    to true and all others to false. Only the selected output is marked
    as "updated" in C.
    
    Inputs:
        - in_0 (EN): Enable signal - block only executes when EN is true
        - in_1 (SEL): Selector value (U8) - index of output to activate
        
    Outputs:
        - q_0..q_N: Boolean outputs (one per case)
    
    
    Usage:
        block = BlockQSelector(
            idx=1,
            ctx=my_context,
            selector=Ref("mode"),
            output_count=3,
            en=Ref("enable")
        )
        # When mode=0 -> q_0=true,  q_1=false, q_2=false
        # When mode=1 -> q_0=false, q_1=true,  q_2=false
        # When mode=2 -> q_0=false, q_1=false, q_2=true
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 selector: Ref,
                 output_count: int,
                 en: Ref = None):
    
        if output_count < 1:
            raise ValueError(f"BlockQSelector idx={idx}: output_count must be >= 1, got {output_count}")
        
        if output_count > 16:
            raise ValueError(f"BlockQSelector idx={idx}: Maximum 16 outputs supported, got {output_count}")
        
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_Q_SELECTOR, ctx=ctx)
        
        self.output_count = output_count
        
        # in_0 = EN, in_1 = selector
        self.add_inputs([en, selector])
        
        # N boolean outputs (all initialized to False)
        outputs = [(mem_types_t.MEM_B,)] * output_count
        self._add_outputs(outputs)
    
    # No custom data packets needed - C code handles the switch logic
    def pack_data(self) -> List[bytes]:
        return []
