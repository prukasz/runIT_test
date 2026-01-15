import struct
from typing import Dict, List, Optional
from BlockBase import BlockBase
from EmulatorMemory import EmulatorMemory

class BlocksStorage:
    def __init__(self, mem_blocks: EmulatorMemory):
        """
        :param mem_blocks: Pamięć kontekstu 1 (wyjścia bloków)
        """
        self.blocks_map: Dict[int, BlockBase] = {}
        self.mem_blocks = mem_blocks # Przechowujemy pamięć tutaj

    def add_block(self, block: BlockBase):
        """Dodaje blok do magazynu i sprawdza unikalność ID."""
        if block.block_idx in self.blocks_map:
            raise ValueError(f"Block ID {block.block_idx} already exists!")
        self.blocks_map[block.block_idx] = block

    def get_block(self, idx: int) -> Optional[BlockBase]:
        return self.blocks_map.get(idx)

    def get_all_blocks_sorted(self) -> List[BlockBase]:
        """Zwraca listę bloków posortowaną po ID."""
        return [self.blocks_map[i] for i in sorted(self.blocks_map.keys())]

    def recalculate_connection_masks(self):
        for blk in self.blocks_map.values():
            mask = 0
            for i, inp in enumerate(blk.inputs):
                if inp is not None:
                    mask |= (1 << i)
            blk.in_connected_mask = mask

    def get_total_count_packet(self) -> bytes:
        count = len(self.blocks_map)
        return struct.pack("<HH", 0xB000, count)

    def __str__(self):
        lines = []
        total_pkt = self.get_total_count_packet()
        lines.append(f"#SYSTEM Total Blocks Count# {total_pkt.hex().upper()}")
        for blk in self.get_all_blocks_sorted():
            lines.append(str(blk))
        return "\n".join(lines)