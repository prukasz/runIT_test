from typing import TextIO
from Variables import VariablesStore
from DumpVariables import VariablesDump
from BlockStorage import BlocksStore
from Enums import Order, Headers
import struct 

class FullDump:
    def __init__(self, var_store: VariablesStore, blk_store: BlocksStore):
        self.var_store = var_store
        self.blk_store = blk_store

    def write(self, writer: TextIO):
        # 1. DUMP VARIABLES (FFFF ... EEEE)
        # We reuse the existing logic which handles the FFFF header,
        # variable counts, table definitions, and EEEE content.
        var_dumper = VariablesDump(self.var_store)
        var_dumper.write(writer)

        # 2. DUMP BLOCK HEADER (00FF + Count)
        # Logic: 00FF is ORD_START_BLOCKS
        # Format: 00FF XXXX (where XXXX is hex count)
        all_blocks = self.blk_store.get_all_blocks()
        block_cnt = len(all_blocks)
        
        # Write Header: 00FF <CountHex>
        # Formatted as 4-char Hex (e.g., 0002)
        writer.write("dddd\n")
        writer.write(f"{Headers.H_BLOCKS_CNT.value:04X} {struct.pack("<H", block_cnt).hex().upper()}\n")

        # 3. DUMP BLOCKS (BB...)
        for blk in all_blocks:
            writer.write(str(blk) + "\n")

        # 4. DUMP TAIL MARKERS
        # Fixed lines as requested: B100, B200, 2137, 1000
        # Corresponds to Enums:
        # B100 -> ORD_EMU_ALLOCATE_BLOCKS_LIST
        # B200 -> ORD_EMU_FILL_BLOCKS_LIST    
        # 2137 -> ORD_EMU_LOOP_INIT           
        # 1000 -> ORD_EMU_LOOP_START          
        
        tail_markers = [
            Order.ORD_EMU_ALLOCATE_BLOCKS_LIST,
            Order.ORD_EMU_FILL_BLOCKS_LIST,
            Order.ORD_EMU_LOOP_INIT,
            Order.ORD_EMU_LOOP_START
        ]

        for marker in tail_markers:
            writer.write(f"{marker.value:04X}\n")