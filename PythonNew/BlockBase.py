import struct
from typing import List, Tuple, Optional, Any, Union
from enum import IntEnum

from EmulatorMemory import EmulatorMemory, DataTypes
from EmulatorMemoryReferences import Global

# Cykliczny import workaround (dla type hinting)
# from BlocksStorage import BlocksStorage 

class BlockType(IntEnum):
    BLOCK_NONE  = 0
    BLOCK_MATH  = 1
    BLOCK_SET_GLOBAL = 2
    BLOCK_CMP   = 3 # Logic
    BLOCK_FOR   = 8
    BLOCK_TIMER = 9
    
    # Dodaj mapowanie, jeśli używasz innych ID w C
    BLOCK_ADD   = 1 # Alias dla kompatybilności wstecznej
    BLOCK_LOGIC = 3 # Alias

class BlockOutputProxy:
    def __init__(self, block_idx: int):
        self.block_idx = block_idx

    def __getitem__(self, idx: int) -> Global:
        alias = f"{self.block_idx}_q{idx}"
        return Global(alias, ref_id=1)

class BlockBase:
    def __init__(self, 
                 block_idx: int, 
                 block_type: BlockType,
                 storage: Any,
                 inputs: List[Union[Global, Any]],
                 output_defs: List[Tuple[DataTypes, List[int]]]
                 ):
        
        self.block_idx = block_idx
        self.block_type = block_type
        self.inputs = inputs
        self.output_defs = output_defs
        
        # 1. Rejestracja wyjść
        self.q_cnt = len(output_defs)
        for i, (dtype, dims) in enumerate(output_defs):
            alias = f"{block_idx}_q{i}"
            storage.mem_blocks.add(alias, dtype, 0, dims=dims)

        # 2. Rejestracja bloku
        storage.add_block(self)

        # 3. Maska (obliczana na podstawie inputs, nie connections!)
        self.in_cnt = len(inputs)
        self.in_connected_mask = 0
        for i, inp in enumerate(inputs):
            if inp is not None:
                self.in_connected_mask |= (1 << i)

        self.out = BlockOutputProxy(block_idx)

    def _pack_common_header(self, subtype: int) -> bytes:
        return struct.pack("<BBBH", 0xBB, self.block_type, subtype, self.block_idx)

    def get_definition_packet(self) -> bytes:
        header = self._pack_common_header(0x00)
        payload = struct.pack("<BHB", self.in_cnt, self.in_connected_mask, self.q_cnt)
        return header + payload

    def get_output_refs_packets(self) -> List[bytes]:
        packets = []
        for i in range(self.q_cnt):
            alias = f"{self.block_idx}_q{i}"
            try:
                ref_node = Global(alias).build()
                node_bytes = ref_node.to_bytes()
                subtype = 0xE0 + i
                packets.append(self._pack_common_header(subtype) + node_bytes)
            except Exception as e:
                print(f"[ERROR] Block {self.block_idx} Output {i}: {e}")
        return packets

    def get_input_refs_packets(self) -> List[bytes]:
        packets = []
        for i, inp in enumerate(self.inputs):
            if inp is None: continue
            try:
                access_node = inp.build()
                node_bytes = access_node.to_bytes()
                subtype = 0xF0 + i
                packets.append(self._pack_common_header(subtype) + node_bytes)
            except Exception as e:
                print(f"[ERROR] Block {self.block_idx} Input {i}: {e}")
        return packets

    def get_custom_data_packet(self) -> List[bytes]:
        return []

    def get_hex_with_comments(self) -> str:
        lines = []
        b_name = self.block_type.name if hasattr(self.block_type, 'name') else self.block_type
        
        # 1. Config
        cfg_hex = self.get_definition_packet().hex().upper()
        lines.append(f"#ID:{self.block_idx} {b_name} Config# {cfg_hex}")

        # 2. Outputs (E0...)
        for pkt in self.get_output_refs_packets():
            hex_str = pkt.hex().upper()
            # Pobierz indeks z bajtu subtype (offset 2)
            subtype = pkt[2]
            idx = subtype - 0xE0
            lines.append(f"#ID:{self.block_idx} Out[{idx}]# {hex_str}")

        # 3. Inputs (F0...)
        for pkt in self.get_input_refs_packets():
            hex_str = pkt.hex().upper()
            # Pobierz indeks z bajtu subtype (offset 2)
            subtype = pkt[2]
            idx = subtype - 0xF0
            lines.append(f"#ID:{self.block_idx} In[{idx}]# {hex_str}")

        return "\n".join(lines)

    def __str__(self):
        base_str = self.get_hex_with_comments()
        custom_pkts = self.get_custom_data_packet()
        if custom_pkts:
            lines = [base_str]
            for i, pkt in enumerate(custom_pkts):
                # Możemy spróbować zgadnąć typ custom packetu
                subtype = pkt[2]
                hex_str = pkt.hex().upper()
                if subtype == 0x01:
                    suffix = "Const/Limits"
                elif subtype == 0x02:
                    suffix = "Code/Settings"
                else:
                    suffix = f"Custom[{subtype:02X}]"
                    
                lines.append(f"#ID:{self.block_idx} {suffix}# {hex_str}")
            return "\n".join(lines)
        return base_str