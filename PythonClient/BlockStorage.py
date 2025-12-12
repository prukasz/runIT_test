from typing import Dict, List, Optional
from Blocks import BlockHandle
from Enums import BlockTypes

class BlocksStore:
    def __init__(self):
        # Primary storage: Map ID -> BlockHandle
        self.blocks_map: Dict[int, BlockHandle] = {}

    def add_block(self, block: BlockHandle) -> bool:
        """
        Stores a BlockHandle.
        Returns False if a block with this block_id already exists.
        """
        if block.block_id in self.blocks_map:
            print(f"[ERROR] Block ID {block.block_id} already exists. Block not added.")
            return False
        
        self.blocks_map[block.block_id] = block
        return True

    def get_block(self, block_id: int) -> Optional[BlockHandle]:
        """Retrieves a block by its manual ID."""
        return self.blocks_map.get(block_id)

    def get_all_blocks(self) -> List[BlockHandle]:
        """Returns a list of all stored blocks, sorted by ID for consistent dumping."""
        # Sorting by ID helps with deterministic export
        return [self.blocks_map[i] for i in sorted(self.blocks_map.keys())]

    def get_blocks_by_type(self, b_type: BlockTypes) -> List[BlockHandle]:
        """Helper to filter blocks by their functional type."""
        return [b for b in self.get_all_blocks() if b.block_type == b_type]

    def remove_block(self, block_id: int) -> bool:
        """Removes a block. No re-indexing is performed on other blocks."""
        if block_id in self.blocks_map:
            del self.blocks_map[block_id]
            return True
        print(f"[ERROR] Cannot remove Block {block_id}: Not found.")
        return False