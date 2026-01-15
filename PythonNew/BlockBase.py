import struct
from typing import List, Tuple, Optional, Any, Union
from Enums import emu_types_t, block_type_t, emu_block_header_t

from EmulatorMemory import EmulatorMemory
from EmulatorMemoryReferences import Ref

# Cykliczny import workaround (dla type hinting)
# from BlocksStorage import BlocksStorage 


class BlockOutputProxy:
    def __init__(self, block_idx: int):
        self.block_idx = block_idx

    def __getitem__(self, idx: int) -> Ref:
        alias = f"{self.block_idx}_q{idx}"
        return Ref(alias, ref_id=1)

class BlockBase:
    def __init__(self, 
                 block_idx: int, 
                 block_type: block_type_t,
                 storage: Any,
                 inputs: List[Union[Ref, Any]],
                 output_defs: List[Tuple[emu_types_t, List[int]]]
                 ):
        
        self.block_idx = block_idx
        self.block_type = block_type
        self.inputs = inputs
        self.output_defs = output_defs
        
        #Outputs Registration
        self.q_cnt = len(output_defs)
        for i, (dtype, dims) in enumerate(output_defs):
            alias = f"{block_idx}_q{i}"
            storage.mem_blocks.add(alias, dtype, 0, dims=dims)

        storage.add_block(self)

        self.in_cnt = len(inputs)
        self.in_connected_mask = 0
        for i, inp in enumerate(inputs):
            if inp is not None:
                self.in_connected_mask |= (1 << i)

        #outputs connection(for others)
        self.out = BlockOutputProxy(block_idx)

    def _pack_common_header(self, subtype: int) -> bytes:
        return struct.pack("<BBBH", emu_block_header_t.EMU_H_BLOCK_START.value, self.block_type, subtype, self.block_idx)

    def get_definition_packet(self) -> bytes:
        header = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CFG.value)
        payload = struct.pack("<BHB", self.in_cnt, self.in_connected_mask, self.q_cnt)
        return header + payload

    def get_output_refs_packets(self) -> List[bytes]:
        packets = []
        for i in range(self.q_cnt):
            alias = f"{self.block_idx}_q{i}"
            try:
                ref_node = Ref(alias).build()
                node_bytes = ref_node.to_bytes()
                subtype = emu_block_header_t.EMU_H_BLOCK_PACKET_OUT_START.value + i
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
                subtype = emu_block_header_t.EMU_H_BLOCK_PACKET_IN_START.value + i
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
            idx = subtype - emu_block_header_t.EMU_H_BLOCK_PACKET_OUT_START.value
            lines.append(f"#ID:{self.block_idx} Out[{idx}]# {hex_str}")

        # 3. Inputs (F0...)
        for pkt in self.get_input_refs_packets():
            hex_str = pkt.hex().upper()
            # Pobierz indeks z bajtu subtype (offset 2)
            subtype = pkt[2]
            idx = subtype - emu_block_header_t.EMU_H_BLOCK_PACKET_IN_START.value
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
                if subtype == emu_block_header_t.EMU_H_BLOCK_PACKET_CONST.value:
                    suffix = "Const/Limits"
                elif subtype == emu_block_header_t.EMU_H_BLOCK_PACKET_CUSTOM.value:
                    suffix = "Code/Settings"
                else:
                    suffix = f"Custom[{subtype:02X}]"
                    
                lines.append(f"#ID:{self.block_idx} {suffix}# {hex_str}")
            return "\n".join(lines)
        return base_str