import struct
from typing import Union, List, Any
from BlockBase import BlockBase
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from Enums import block_type_t, emu_types_t

class BlockSet(BlockBase):
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 target: Ref,
                 value: Ref):
        
        if not isinstance(target, Ref):
            raise ValueError(f"BlockSet ID {block_idx}: Target (Input 0) must be a Reference (Ref), got {type(target)}")
        if target.ref_id != 1:
             print(f"[WARNING] BlockSet ID {block_idx}: Target Ref ID is {target.ref_id}, expected 1 (Global)")

        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_SET,
            storage=storage,
            inputs=[target, value],
            output_defs=[] 
        )

    def get_custom_data_packet(self) -> List[bytes]:
        return []

    def __str__(self):
        return self.get_hex_with_comments()
