from dataclasses import dataclass, field
from typing import List, Dict
import struct
from Enums import DataTypes, BlockTypes, DataTypesSizes, Headers
from GlobalReferences import Global_reference

# Formatter
def val_to_hex(data: bytes, size: int = 1) -> str:
    hex_str = data.hex().upper()
    step = size * 2
    parts = [hex_str[i:i+step] for i in range(0, len(hex_str), step)]
    return " ".join(parts)

@dataclass
class QConnection:
    target_blocks_idx_list: List[int] = field(default_factory=list)
    target_inputs_num_list: List[int] = field(default_factory=list)

    def add_target(self, block_idx: int, input_idx: int):
        self.target_blocks_idx_list.append(block_idx)
        self.target_inputs_num_list.append(input_idx)

    def __str__(self) -> str:
        parts = []
        
        # Connection Count
        conn_cnt = len(self.target_blocks_idx_list)
        parts.append("#conn_cnt#")
        parts.append(val_to_hex(struct.pack('<B', conn_cnt), 1))

        # Pairs: Target(u16) + Input(u8)
        for b_id, i_idx in zip(self.target_blocks_idx_list, self.target_inputs_num_list):
            b_id_hex = val_to_hex(struct.pack('<H', b_id), 2)
            i_idx_hex = val_to_hex(struct.pack('<B', i_idx), 1)
            parts.append(f"{b_id_hex} {i_idx_hex}")

        return " ".join(parts)

@dataclass
class BlockBase:
    block_idx: int
    block_type: BlockTypes
    
    in_data_type_table: List[DataTypes] = field(default_factory=list)
    q_data_type_table: List[DataTypes] = field(default_factory=list)
    
    in_used_mask: int = 0
    in_global_mask: int = 0
    q_connections: List[QConnection] = field(default_factory=list)
    global_references: List[Global_reference] = field(default_factory=list)

    def __post_init__(self):
        self.q_connections = [QConnection() for _ in range(len(self.q_data_type_table))]
        self._global_map: Dict[int, Global_reference] = {}

    def connect(self, output_idx: int, target_block: 'BlockBase', target_input_idx: int):
        if output_idx >= len(self.q_connections):
            raise ValueError(f"Block {self.block_idx}: No output {output_idx}")
        if target_input_idx >= len(target_block.in_data_type_table):
             raise ValueError(f"Target Block {target_block.block_idx}: No input {target_input_idx}")
        if (target_block.in_used_mask & (1 << target_input_idx)) != 0:
            raise ValueError(f"Connection Error: Input {target_input_idx} occupied!")

        self.q_connections[output_idx].add_target(target_block.block_idx, target_input_idx)
        target_block.in_used_mask |= (1 << target_input_idx)
        
    def add_global_connection(self, input_idx: int, ref: Global_reference):
        if input_idx >= 16:
            raise ValueError(f"Block {self.block_idx}: Input {input_idx} out of range for global connection")

        self._global_map[input_idx] = ref
        self.in_global_mask |= (1 << input_idx)

        sorted_indices = sorted(self._global_map.keys())
        self.global_references = [self._global_map[idx] for idx in sorted_indices]

    def get_extra_data(self) -> str:
        return ""

    def __str__(self) -> str:
        lines = []
        
        in_cnt = len(self.in_data_type_table)
        in_size = sum(DataTypesSizes.get(dt) for dt in self.in_data_type_table)
        q_cnt = len(self.q_data_type_table)
        q_size = sum(DataTypesSizes.get(dt) for dt in self.q_data_type_table)
        conn_cnt = len(self.q_connections)

        parts = []

        #HEADER [BB] [Type] [00]
        parts.append(val_to_hex(struct.pack('<BBB', 0xBB, int(self.block_type), 0x00), 1))
        
        # Block ID (u16) -> XXXX
        parts.append("#B_idx#")
        parts.append(val_to_hex(struct.pack('<H', self.block_idx), 2))
        
        parts.append("#in_used#")
        parts.append(val_to_hex(struct.pack('<H', self.in_used_mask), 2))
        
        parts.append("#in_cnt/size#")
        parts.append(val_to_hex(struct.pack('<BB', in_cnt, in_size), 1))
        
        parts.append("#in_types#")
        if self.in_data_type_table:
            type_bytes = b''.join([struct.pack('<B', int(dt)) for dt in self.in_data_type_table])
            parts.append(val_to_hex(type_bytes, 1))

        # Outputs
        parts.append("#q_cnt/size#")
        parts.append(val_to_hex(struct.pack('<BB', q_cnt, q_size), 1)) 
        
        parts.append("#q_types#")
        if self.q_data_type_table:
            type_bytes = b''.join([struct.pack('<B', int(dt)) for dt in self.q_data_type_table])
            parts.append(val_to_hex(type_bytes, 1))

        # Connections
        parts.append("#conn_cnt#")
        parts.append(val_to_hex(struct.pack('<B', conn_cnt), 1))

        for idx, conn in enumerate(self.q_connections):
            parts.append(f"#qconn{idx}#")
            parts.append(str(conn))
        
        lines.append(" ".join(parts))


        # Global References
        h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, int(self.block_type), int(Headers.H_BLOCK_START_G_ACCES_MASK)), 1)
        h_id = val_to_hex(struct.pack('<H', self.block_idx), 2)
        h_mask = val_to_hex(struct.pack('<H', self.in_global_mask), 2)
        lines.append(f"#G_ACC# {h_prefix} {h_id} {h_mask}")
        for idx, ref in enumerate(self.global_references):
            if ref is None: continue
            ref_marker = int(Headers.H_START_REFERENCE) + idx
            
            # Header: BB Type Marker(u8) BlockID(u16)
            h_prefix = val_to_hex(struct.pack('<BBB', 0xBB, int(self.block_type), ref_marker), 1)
            h_id = val_to_hex(struct.pack('<H', self.block_idx), 2)
            
            alias_name = getattr(ref, 'target_alias', f'REF_{idx}')
            lines.append(f"#REF {alias_name}# {h_prefix} {h_id} {str(ref)}")

        # Extra Data
        extra = self.get_extra_data()
        if extra:
            lines.append(extra)

        return "\n".join(lines)