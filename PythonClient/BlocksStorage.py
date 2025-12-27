from typing import Dict, List, Optional
from Blocks import BlockHandle
from Enums import BlockTypes

class BlocksStorage:
    def __init__(self):
        # Primary storage: Map ID -> BlockHandle
        self.blocks_map: Dict[int, BlockHandle] = {}

    def add_block(self, block: BlockHandle) -> bool:
        """
        Stores a BlockHandle.
        Returns False if a block with this block_id already exists.
        """
        if block.block_idx in self.blocks_map:
            print(f"[ERROR] Block ID {block.block_idx} already exists. Block not added.")
            return False
        
        self.blocks_map[block.block_idx] = block
        return True

    def get_block(self, block_id: int) -> Optional[BlockHandle]:
        """Retrieves a block by its manual ID"""
        return self.blocks_map.get(block_id)

    def get_all_blocks(self) -> List[BlockHandle]:
        """Returns a list of all stored blocks, sorted by ID for consistent dumping"""
        return [self.blocks_map[i] for i in sorted(self.blocks_map.keys())]

    def get_blocks_by_type(self, b_type: BlockTypes) -> List[BlockHandle]:
        """Helper to filter blocks by their functional type."""
        return [b for b in self.get_all_blocks() if b.block_type == b_type]

    def remove_block(self, block_id: int) -> bool:
        """Removes a block. No re-indexing is performed on other blocks."""
        if block_id in self.blocks_map:
            del self.blocks_map[block_id]
            return True
        print(f"[BlockStorage] Cannot remove Block {block_id}: Not found")
        return False

    def set_used_inputs(self):
        """
        Iterates through all block connections and sets the 'in_used' bitmask
        on target blocks for each input that is connected.
        """
        # Reset all in_used fields first to handle potential removals/changes
        for block in self.blocks_map.values():
            block.in_used = 0

        # Go through each block and its connections
        for source_block in self.blocks_map.values():
            for q_connection in source_block.q_connections_table:
                if q_connection is None:
                    continue

                for target_block_idx, target_input_num in zip(q_connection.target_blocks_idx_list, q_connection.target_inputs_num_list):
                    target_block = self.get_block(target_block_idx)
                    if target_block:
                        target_block.in_used |= (1 << target_input_num)
                    else:
                        print(f"[WARNING] Block {source_block.block_idx} has a connection to non-existent block ID {target_block_idx}.")