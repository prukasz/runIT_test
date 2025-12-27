from typing import Dict, List, Optional
from BlockBase import BlockBase

class BlocksStorage:
    def __init__(self):
        self.blocks_map: Dict[int, BlockBase] = {}

    def add_block(self, block: BlockBase):
        if block.block_idx in self.blocks_map:
            raise ValueError(f"Block ID {block.block_idx} already exists!")
        self.blocks_map[block.block_idx] = block

    def get_block(self, idx: int) -> Optional[BlockBase]:
        return self.blocks_map.get(idx)

    def get_all_blocks(self) -> List[BlockBase]:
        return [self.blocks_map[i] for i in sorted(self.blocks_map.keys())]

    def recalculate_used_inputs(self):
        """
        Przechodzi po wszystkich połączeniach i ustawia maski in_used w blokach docelowych.
        Należy wywołać to RAZ przed generowaniem dumpu.
        """
        # Reset masek
        for b in self.blocks_map.values():
            b.in_used_mask = 0
            
        # Analiza połączeń
        for source_blk in self.blocks_map.values():
            for conn in source_blk.q_connections:
                for target_id, target_in in zip(conn.target_blocks_idx_list, conn.target_inputs_num_list):
                    target = self.get_block(target_id)
                    if target:
                        target.in_used_mask |= (1 << target_in)