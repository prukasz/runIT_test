from typing import TextIO
from GlobalStorage import GlobalStorage
from BlockStorage import BlocksStorage
from Enums import Order, Headers
import struct 

class FullDump:
    def __init__(self, var_store: GlobalStorage, blk_store: BlocksStorage):
        self.var_store = var_store
        self.blk_store = blk_store

    def write(self, writer: TextIO):
        # ==========================================
        # 1. VARIABLE DEFINITIONS (FFFF ... EEEE)
        # ==========================================
        # Retrieve the header strings directly from storage
        writer.write(self.var_store.get_dump_list_string() + "\n")

        # ==========================================
        # 2. VARIABLE CONTENT VALUES
        # ==========================================
        # Retrieve the value strings directly from storage
        content_str = self.var_store.get_dump_content_string()
        if content_str:
            writer.write(content_str + "\n")

        # ==========================================
        # 3. BLOCKS HEADER
        # ==========================================
        all_blocks = self.blk_store.get_all_blocks()
        block_cnt = len(all_blocks)
        
        # Write Divider (DDDD)
        writer.write("DDDD\n")

        # Write Count Header: H_BLOCKS_CNT (00FF) + Count (Hex Little Endian)
        # struct.pack("<H", block_cnt) handles the unsigned short conversion
        cnt_hex = struct.pack("<H", block_cnt).hex().upper()
        writer.write(f"{Headers.H_BLOCKS_CNT.value:04X} {cnt_hex}\n")

        # ==========================================
        # 4. BLOCKS CONTENT
        # ==========================================
        # Iterates over blocks in insertion order (guaranteed by BlocksStorage logic)
        for blk in all_blocks:
            writer.write(str(blk) + "\n")

        # ==========================================
        # 5. TAIL MARKERS / EXECUTION ORDER
        # ==========================================
        tail_markers = [
            Order.ORD_EMU_CREATE_BLOCK_LIST, 
            Order.ORD_EMU_CREATE_BLOCKS,     
            Order.ORD_EMU_FILL_BLOCKS,       
            Order.ORD_EMU_LOOP_INIT,            # 2137
            Order.ORD_EMU_LOOP_START            # 1000
        ]

        for marker in tail_markers:
            writer.write(f"{marker.value:04X}\n")