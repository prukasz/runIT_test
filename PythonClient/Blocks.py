from dataclasses import dataclass, field
from Enums import DataTypes, BlockTypes
from typing import List, Optional
from References import Global_reference, Ref
from Variables import VariablesStore
from DumpVariables import VariablesDump
import struct
from MathExpressionBuilder import MathExpression

@dataclass
class QConnection:
    conn_cnt: int = 0
    target_blocks_id_list: List[int] = field(default_factory=list)
    target_inputs_list: List[int] = field(default_factory=list)
    def __post_init__(self):
        self.conn_cnt = len(self.target_blocks_id_list)
    def __str__(self)-> str:
        packed_bytes = struct.pack('<B', self.conn_cnt)
        for block_id, input_idx in zip(self.target_blocks_id_list, self.target_inputs_list):
            packed_bytes += struct.pack('<HB', block_id, input_idx)
        return packed_bytes.hex().upper()


@dataclass
class BlockHandle:
    block_id: int
    block_type: BlockTypes
    
    in_data_type_table: List[DataTypes] = field(default_factory=list)
    q_data_type_table: List[DataTypes] = field(default_factory=list)
    
    q_connections_table: List[Optional[QConnection]] = field(default_factory=list)
    # global_reference pomijam w serializacji zgodnie z opisem
    
    # Pola wyliczane
    in_cnt: int = field(init=False)
    q_cnt: int = field(init=False)
    global_reference: List['Global_reference'] = field(default_factory=list)

    def __post_init__(self):
        self.in_cnt = len(self.in_data_type_table)
        self.q_cnt = len(self.q_data_type_table)
        
        # Wypełnienie tabli połączeń None/Pustymi obiektami, jeśli jest krótsza niż liczba wyjść
        while len(self.q_connections_table) < self.q_cnt:
            self.q_connections_table.append(QConnection())

    def __str__(self) -> str:
        lines = []

        # --- 1. GŁÓWNA DEFINICJA BLOKU (Linia 1) ---
        # Header: BB + Type + 00 + ID
        header = struct.pack('<BBBH', 0xBB, int(self.block_type), 0x00, self.block_id)

        # Inputs
        inputs_data = struct.pack('<B', self.in_cnt) + b''.join([struct.pack('<B', int(dt)) for dt in self.in_data_type_table])
        
        # Outputs
        outputs_data = struct.pack('<B', self.q_cnt) + b''.join([struct.pack('<B', int(dt)) for dt in self.q_data_type_table])
        
        # Connections Header
        conn_len_header = struct.pack('<B', len(self.q_connections_table))
        
        # Złożenie bazy
        main_hex = (header + inputs_data + outputs_data + conn_len_header).hex().upper()

        # Doklejenie stringów z połączeń
        for conn in self.q_connections_table:
            main_hex += "00" if conn is None else str(conn)
            
        lines.append(main_hex)

        # --- 2. GLOBAL REFERENCES (Kolejne linie) ---
        for idx, ref in enumerate(self.global_reference):
            if ref is None: 
                continue

            # Format: BB + BlockType + (0xF0 + Index) + BlockID
            # Marker 0xF0 oznacza start indeksowania referencji
            ref_marker = 0xF0 + idx
            
            # Pack: BB (1B), Type (1B), Marker (1B), ID (2B, Little Endian)
            ref_header = struct.pack('<BBBH', 0xBB, int(self.block_type), ref_marker, self.block_id)
            
            # Złożenie linii: Nagłówek HEX + Wynik str() z obiektu referencji
            ref_line = ref_header.hex().upper() + str(ref)
            lines.append(ref_line)

        # Zwracamy wszystkie linie oddzielone znakiem nowej linii
        return "\n".join(lines)

# --- PRZYKŁAD UŻYCIA ---
if __name__ == "__main__":

    store = VariablesStore()

    store.add_scalar("scalar1", DataTypes.DATA_UI8, 1)
    store.add_scalar("scalar2", DataTypes.DATA_UI8, 2)
    store.add_scalar("scalar3", DataTypes.DATA_UI8, 3)

    store.add_array("array1", DataTypes.DATA_UI16, [1, 2, 3, 4], [4])
    store.add_array("array2", DataTypes.DATA_UI16, [1, 2, 3, 4], [4])
    store.add_array("array3", DataTypes.DATA_UI16, [1, 2, 3, 4], [4])
 
    store.add_array("target_data", DataTypes.DATA_UI32, [18]*64, [4,4,4])

    ref1 =Ref("target_data")[0,1,2].build(store)
    
    ref2 =Ref("target_data")[
        Ref("array1")[Ref("scalar1")],
        Ref("array2")[Ref("scalar1")],
        Ref("array3")[Ref("scalar1")]
        ].build(store)
    
    filename = "valuedump.txt"
    print(f"Dumping variables to '{filename}'...")
    
    dumper = VariablesDump(store)
    with open(filename, "w") as f:
        dumper.write(f)
    print("Dump Complete.")

    conn1 = QConnection(target_blocks_id_list=[0], target_inputs_list=[0])
    
    blk = BlockHandle(
        block_id=0x0000,    
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_F],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn1],
        global_reference=[ref1, ref2]
    )
    
    

    print(blk)
    expr = MathExpression("(in_1 + in_2) * 2.5", block_id=1)
    expr2 = MathExpression("in_2 + in_3 - in_1 * 4 / sqrt(9)", block_id=2)
    print(expr)
    print(expr2)



