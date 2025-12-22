from dataclasses import dataclass, field
from Enums import DataTypes, BlockTypes, DataTypesSizes
from typing import List, Optional, Any
from References import Global_reference
import struct

@dataclass
class QConnection:
    '''This dataclass is responsible for one block output connections to other blocks \n
    Each connection is represented as list of pairs: (target block_idx and target_in_num in this block)
    '''
    conn_cnt: int = 0
    target_blocks_idx_list: List[int] = field(default_factory=list)
    target_inputs_num_list: List[int] = field(default_factory=list)
    def __post_init__(self):
        #Automatically count connections from oone output
        self.conn_cnt = len(self.target_blocks_idx_list)
    def __str__(self)-> str:
        #Packing to Hex 
        #1. Pack Conn_cnt
        conn_cnt = struct.pack('<B', self.conn_cnt)
        #2. Pack pairs of target_idx and target_in_num
        for block_id, input_idx in zip(self.target_blocks_idx_list, self.target_inputs_num_list):
            conn_cnt += struct.pack('<HB', block_id, input_idx)
        #3. Return result

        return conn_cnt.hex().upper()


@dataclass
class BlockHandle:
    block_idx: int
    block_type: BlockTypes
    #list of all inputs 
    in_used: int = 0
    in_data_type_table: List[DataTypes] = field(default_factory=list)
    in_cnt: int = field(init=False)
    #list of all outputs
    q_data_type_table: List[DataTypes] = field(default_factory=list)
    q_cnt: int = field(init=False)
    #list of all connections outgoing form block
    q_connections_table: List[Optional[QConnection]] = field(default_factory=list)
    #list of references to global variables
    global_reference: List['Global_reference'] = field(default_factory=list)
    #block data
    block_data: Optional[Any] = field(default=None)

    def __post_init__(self):
        self.in_cnt = len(self.in_data_type_table)
        self.q_cnt = len(self.q_data_type_table)
        self.in_used = 0
        self.in_used = (1 << self.in_cnt) - 1 if self.in_cnt > 0 else 0
        #add empty Qconnections
        while len(self.q_connections_table) < self.q_cnt:
            self.q_connections_table.append(QConnection())

    def __str__(self) -> str:
        lines = []
        # Header [0xBB(common),0xXX(block_type), 0x00(common), 0xXXXX(block_idx)]
        header = struct.pack('<BBBH', 0xBB, int(self.block_type), 0x00, self.block_idx)

        # Inputs
        in_used = struct.pack('<H', self.in_used)
        inputs_data = struct.pack('<B', self.in_cnt) + struct.pack('<B', sum(DataTypesSizes[dt] for dt in self.in_data_type_table))+ b''.join([struct.pack('<B', int(dt)) for dt in self.in_data_type_table])

        # Outputs
        outputs_data = struct.pack('<B', self.q_cnt) + struct.pack('<B', sum(DataTypesSizes[dt] for dt in self.q_data_type_table))+ b''.join([struct.pack('<B', int(dt)) for dt in self.q_data_type_table])
        # Connections Header
        conn_len_header = struct.pack('<B', len(self.q_connections_table))
        
        # Block struct representation
        main_hex = (header + in_used + inputs_data + outputs_data + conn_len_header).hex().upper()

        # Add Qconnectins 
        for conn in self.q_connections_table:
            main_hex += "00" if conn is None else str(conn)
        # This is first line    
        lines.append(main_hex) 

        # Add global references
        for idx, ref in enumerate(self.global_reference):
            if ref is None: 
                continue
            #reference packet index
            ref_marker = 0xF0 + idx
            
            # Header [0xBB(common),0xXX(block_type), 0x00(common), 0xXXXX(block_idx)]
            ref_header = struct.pack('<BBBH', 0xBB, int(self.block_type), ref_marker, self.block_idx)
            
            # Header + Reference as string
            ref_line = ref_header.hex().upper() + str(ref)
            lines.append(ref_line)
        # Return as one string separated by new line
        if self.block_data is not None:
             lines.append(str(self.block_data))
             
        return "\n".join(lines)
