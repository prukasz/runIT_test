import struct
from typing import TextIO, List
from enum import IntEnum

from EmulatorMemory import EmulatorMemory
from BlocksStorage import BlocksStorage

# =============================================================================
# ENUM ROZKAZÓW (Zgodny z C: emu_order_t)
# =============================================================================
class Order(IntEnum):
    ORD_STOP_BYTES            = 0x0000    
    ORD_START_BYTES           = 0xFFFF
    
    # Variables
    ORD_PARSE_VARIABLES       = 0xEEEE  # Alloc Memory
    ORD_PARSE_INTO_VARIABLES  = 0xDDDD  # Fill Memory Data
    
    # Blocks
    ORD_EMU_CREATE_BLOCK_LIST = 0xB100  # Alloc Block List
    ORD_EMU_CREATE_BLOCKS     = 0xB200  # Parse & Config Blocks
    ORD_EMU_FILL_BLOCKS       = 0xB300
    
    # Utilities
    ORD_RESET_ALL             = 0x0001 
    ORD_RESET_BLOCKS          = 0x0002
    ORD_RESET_MGS_BUF         = 0x0003 
    
    # Loop Execution
    ORD_EMU_LOOP_INIT         = 0x3000
    ORD_EMU_LOOP_START        = 0x1000
    ORD_EMU_LOOP_STOP         = 0x2000

# =============================================================================
# KLASA FULL DUMP
# =============================================================================
class FullDump:
    def __init__(self, mem_glob: EmulatorMemory, mem_blk: EmulatorMemory, blk_storage: BlocksStorage):
        self.mem_glob = mem_glob
        self.mem_blk = mem_blk
        self.blk_storage = blk_storage

    def _write_line(self, writer: TextIO, content: str):
        writer.write(content + "\n")

    def _write_order(self, writer: TextIO, order: Order, comment: str = ""):
        ord_bytes = struct.pack("<H", order.value)
        hex_str = ord_bytes.hex().upper()
        if comment:
            self._write_line(writer, f"#{comment}# {hex_str}")
        else:
            self._write_line(writer, f"#ORDER {order.name}# {hex_str}")

    def _write_raw_order(self, writer: TextIO, order: Order):
        """Wypisuje sam HEX rozkazu bez komentarza (dla 0300)"""
        ord_bytes = struct.pack("<H", order.value)
        hex_str = ord_bytes.hex().upper()
        self._write_line(writer, hex_str)

    def _write_packets(self, writer: TextIO, packets: List[bytes], context_name: str):
        for p in packets:
            hex_str = p.hex().upper()
            self._write_line(writer, f"#{context_name}# {hex_str}")

    def write(self, writer: TextIO):
        """
        Generuje pełny zrzut systemu zgodnie z nowym schematem.
        """
        
        # Helper: Czy to pakiet konfiguracyjny (Size/Count/ArrDef)?
        def is_config_pkt(pkt):
            if len(pkt) < 2: return False
            h = struct.unpack("<H", pkt[0:2])[0]
            return h in [0xFF00, 0xFF01, 0xFF02]

        # Pobranie pakietów
        glob_pkts = self.mem_glob.get_dump_bytes()
        blk_pkts  = self.mem_blk.get_dump_bytes()

        # ==========================================
        # 1. GLOBAL VARIABLES ALLOCATION (Context 0)
        # ==========================================
        alloc_glob = [p for p in glob_pkts if is_config_pkt(p)]
        self._write_packets(writer, alloc_glob, "Global Alloc (Ctx0)")
        
        self._write_order(writer, Order.ORD_PARSE_VARIABLES)
        self._write_raw_order(writer, Order.ORD_RESET_MGS_BUF) # 0300 - Czysty bufor dla kolejnej partii

        # ==========================================
        # 2. BLOCK VARIABLES ALLOCATION (Context 1)
        # ==========================================
        alloc_blk = [p for p in blk_pkts if is_config_pkt(p)]
        self._write_packets(writer, alloc_blk, "Blocks Alloc (Ctx1)")
        
        self._write_order(writer, Order.ORD_PARSE_VARIABLES)
        self._write_raw_order(writer, Order.ORD_RESET_MGS_BUF) # 0300

        # ==========================================
        # 3. FILL VARIABLES (Values)
        # ==========================================
        # Global Data
        data_glob = [p for p in glob_pkts if not is_config_pkt(p)]
        self._write_packets(writer, data_glob, "Global Data (Ctx0)")
        
        # Block Data (Inicjalizacja zerami/wartościami początkowymi)
        data_blk = [p for p in blk_pkts if not is_config_pkt(p)]
        self._write_packets(writer, data_blk, "Blocks Data (Ctx1)")
        
        self._write_order(writer, Order.ORD_PARSE_INTO_VARIABLES)

        # ==========================================
        # 4. BLOCKS LIST
        # ==========================================
        # Pakiet B000 - Liczba bloków
        cnt_pkt = self.blk_storage.get_total_count_packet()
        self._write_line(writer, f"#SYSTEM Total Blocks Count# {cnt_pkt.hex().upper()}")
        
        # Rozkaz alokacji listy
        self._write_order(writer, Order.ORD_EMU_CREATE_BLOCK_LIST)

        # ==========================================
        # 5. BLOCKS DEFINITION (Config + Custom)
        # ==========================================
        # String wszystkich bloków
        self._write_line(writer, str(self.blk_storage))
        
        # Rozkaz parsowania/tworzenia bloków
        self._write_order(writer, Order.ORD_EMU_CREATE_BLOCKS)

        # ==========================================
        # 6. RUN LOOP
        # ==========================================
        self._write_order(writer, Order.ORD_EMU_LOOP_INIT)
        self._write_order(writer, Order.ORD_EMU_LOOP_START)