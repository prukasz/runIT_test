import struct
from typing import List, Any, Dict, Optional, Union
from collections import defaultdict
from dataclasses import dataclass, field
from enum import IntEnum

# =============================================================================
# 1. KONFIGURACJA I ENUMY (ZGODNE Z PLIKAMI C)
# =============================================================================

class DataTypes(IntEnum):
    DATA_UI8  = 0
    DATA_UI16 = 1
    DATA_UI32 = 2
    DATA_I8   = 3
    DATA_I16  = 4
    DATA_I32  = 5
    DATA_F    = 6
    DATA_D    = 7
    DATA_B    = 8

class Headers(IntEnum):
    # Config Packets
    VAR_H_SIZES      = 0xFF00
    VAR_H_SCALAR_CNT = 0xFF01
    VAR_H_ARR        = 0xFF02
    
    # Data Packets (Scalars)
    VAR_H_DATA_S_UI8  = 0x0F10 
    VAR_H_DATA_S_UI16 = 0x0F20
    VAR_H_DATA_S_UI32 = 0x0F30
    VAR_H_DATA_S_I8   = 0x0F40
    VAR_H_DATA_S_I16  = 0x0F50
    VAR_H_DATA_S_I32  = 0x0F60
    VAR_H_DATA_S_F    = 0x0F70
    VAR_H_DATA_S_D    = 0x0F80
    VAR_H_DATA_S_B    = 0x0F90

    # Data Packets (Arrays)
    VAR_H_DATA_ARR_UI8  = 0xFFF0
    VAR_H_DATA_ARR_UI16 = 0xFFF1
    VAR_H_DATA_ARR_UI32 = 0xFFF2
    VAR_H_DATA_ARR_I8   = 0xFFF3
    VAR_H_DATA_ARR_I16  = 0xFFF4
    VAR_H_DATA_ARR_I32  = 0xFFF5
    VAR_H_DATA_ARR_F    = 0xFFF6
    VAR_H_DATA_ARR_D    = 0xFFF7
    VAR_H_DATA_ARR_B    = 0xFFF8

# Mapowanie typów na formaty struct i nagłówki
TYPE_CONFIG = {
    DataTypes.DATA_UI8:  {"fmt": "B", "size": 1, "h_scal": Headers.VAR_H_DATA_S_UI8,  "h_arr": Headers.VAR_H_DATA_ARR_UI8},
    DataTypes.DATA_UI16: {"fmt": "H", "size": 2, "h_scal": Headers.VAR_H_DATA_S_UI16, "h_arr": Headers.VAR_H_DATA_ARR_UI16},
    DataTypes.DATA_UI32: {"fmt": "I", "size": 4, "h_scal": Headers.VAR_H_DATA_S_UI32, "h_arr": Headers.VAR_H_DATA_ARR_UI32},
    DataTypes.DATA_I8:   {"fmt": "b", "size": 1, "h_scal": Headers.VAR_H_DATA_S_I8,   "h_arr": Headers.VAR_H_DATA_ARR_I8},
    DataTypes.DATA_I16:  {"fmt": "h", "size": 2, "h_scal": Headers.VAR_H_DATA_S_I16,  "h_arr": Headers.VAR_H_DATA_ARR_I16},
    DataTypes.DATA_I32:  {"fmt": "i", "size": 4, "h_scal": Headers.VAR_H_DATA_S_I32,  "h_arr": Headers.VAR_H_DATA_ARR_I32},
    DataTypes.DATA_F:    {"fmt": "f", "size": 4, "h_scal": Headers.VAR_H_DATA_S_F,    "h_arr": Headers.VAR_H_DATA_ARR_F},
    DataTypes.DATA_D:    {"fmt": "d", "size": 8, "h_scal": Headers.VAR_H_DATA_S_D,    "h_arr": Headers.VAR_H_DATA_ARR_D},
    DataTypes.DATA_B:    {"fmt": "?", "size": 1, "h_scal": Headers.VAR_H_DATA_S_B,    "h_arr": Headers.VAR_H_DATA_ARR_B},
}

# =============================================================================
# 2. KLASA ZMIENNEJ
# =============================================================================

@dataclass
class Variable:
    alias: str
    dtype: DataTypes
    value: Any          
    dims: List[int]     # [] = Scalar, [x, y] = Array
    
    # Pola wyliczane automatycznie
    index: int = field(init=False, default=0)
    is_array: bool = field(init=False)
    flat_values: List[Any] = field(init=False)
    
    def __post_init__(self):
        self.is_array = len(self.dims) > 0
        
        # Spłaszczanie i dopasowanie danych
        if self.is_array:
            self.flat_values = self._flatten(self.value)
            
            # Obliczanie całkowitego rozmiaru
            total_elements = 1
            for d in self.dims: total_elements *= d
            
            # Padding lub przycięcie
            current_len = len(self.flat_values)
            if current_len < total_elements:
                self.flat_values.extend([0] * (total_elements - current_len))
            elif current_len > total_elements:
                self.flat_values = self.flat_values[:total_elements]
        else:
            self.flat_values = [self.value]

    def _flatten(self, val) -> List[Any]:
        if not isinstance(val, list): return [val]
        out = []
        for v in val: out.extend(self._flatten(v))
        return out

# =============================================================================
# 3. MANAGER PAMIĘCI (LOGIKA)
# =============================================================================

class EmulatorMemory:
    def __init__(self, context_id: int = 0):
        """
        :param context_id: ID pamięci (0=Globals, 1=Blocks, etc.)
        """
        self.context_id = context_id
        self.variables: Dict[DataTypes, List[Variable]] = defaultdict(list)
        self.alias_map = {}

    def add(self, alias: str, dtype: DataTypes, value: Any, dims: List[int] = []) -> bool:
        if alias in self.alias_map:
            print(f"[ERROR] Alias '{alias}' already exists in Ctx {self.context_id}.")
            return False
            
        var = Variable(alias, dtype, value, dims)
        self.variables[dtype].append(var)
        self.alias_map[alias] = var
        return True

    def get_var_index(self, alias: str) -> Optional[int]:
        """Zwraca indeks zmiennej po aliasie (potrzebne do referencji w blokach)."""
        self.recalculate_indices() # Upewnij się, że indeksy są aktualne
        if alias in self.alias_map:
            return self.alias_map[alias].index
        return None

    def recalculate_indices(self):
        """Sortuje zmienne i nadaje indeksy. Zasada: Skalary, potem Tablice."""
        for dtype in DataTypes:
            vars_list = self.variables[dtype]
            
            scalars = [v for v in vars_list if not v.is_array]
            arrays  = [v for v in vars_list if v.is_array]
            
            current_idx = 0
            for var in scalars:
                var.index = current_idx
                current_idx += 1
            for var in arrays:
                var.index = current_idx
                current_idx += 1

    def get_dump_bytes(self, max_size: int = 240) -> List[bytes]:
        """Generuje listę pakietów binarnych."""
        self.recalculate_indices()
        packets = []

        # --- Helper: Header Pack (2B Header + 1B Ctx) ---
        def pack_head(h_enum):
            return struct.pack("<HB", h_enum, self.context_id)

        # ---------------------------------------------------------
        # 1. CONFIG PACKET (Header: 0xFF00)
        # Format: [H] [Ctx] [VarCnts...] [InstCnts...] [ExtraDims...]
        # ---------------------------------------------------------
        var_counts = bytearray()
        inst_counts = bytearray()
        extra_dims = bytearray() # Arena bytes for flexible array members

        for dtype in DataTypes:
            vars_list = self.variables[dtype]
            
            # A. Total elements (Pool Size)
            total_elems = sum(len(v.flat_values) for v in vars_list)
            var_counts.extend(struct.pack("<H", total_elems))
            
            # B. Instance count (Lookup Size)
            inst_counts.extend(struct.pack("<H", len(vars_list)))
            
            # C. Extra Dims Bytes (Arena Size needed for dim_sizes array)
            # Każda tablica potrzebuje `dims_cnt * 1 byte` w arenie
            dims_bytes = sum(len(v.dims) for v in vars_list if v.is_array)
            extra_dims.extend(struct.pack("<H", dims_bytes))

        config_payload = var_counts + inst_counts + extra_dims
        
        # Check size (Config is usually small)
        packets.append(pack_head(Headers.VAR_H_SIZES) + config_payload)

        # ---------------------------------------------------------
        # 2. SCALAR COUNT PACKET (Header: 0xFF01)
        # Format: [H] [Ctx] [Cnt_UI8] [Cnt_UI16] ...
        # ---------------------------------------------------------
        scalar_payload = bytearray()
        for dtype in DataTypes:
            cnt = len([v for v in self.variables[dtype] if not v.is_array])
            scalar_payload.extend(struct.pack("<H", cnt))
            
        packets.append(pack_head(Headers.VAR_H_SCALAR_CNT) + scalar_payload)

        # ---------------------------------------------------------
        # 3. ARRAY DEFINITION PACKETS (Header: 0xFF02)
        # Format: [H] [Ctx] | [DimsCnt] [Type] [D0] [D1]... | ...
        # ---------------------------------------------------------
        arr_buffer = bytearray()
        curr_buf_len = 3 # Header(2) + Ctx(1)
        
        for dtype in DataTypes:
            arrays = [v for v in self.variables[dtype] if v.is_array]
            for arr in arrays:
                # [DimsCnt: 1B] [Type: 1B] [DimSizes: N Bytes]
                def_bytes = struct.pack("BB", len(arr.dims), dtype.value)
                for d in arr.dims: def_bytes += struct.pack("B", d)
                
                # Check MTU
                if (curr_buf_len + len(def_bytes)) > max_size:
                    packets.append(pack_head(Headers.VAR_H_ARR) + arr_buffer)
                    arr_buffer = bytearray()
                    curr_buf_len = 3
                
                arr_buffer.extend(def_bytes)
                curr_buf_len += len(def_bytes)
        
        if len(arr_buffer) > 0:
            packets.append(pack_head(Headers.VAR_H_ARR) + arr_buffer)

        # ---------------------------------------------------------
        # 4. DATA PACKETS (VALUES)
        # ---------------------------------------------------------
        for dtype in DataTypes:
            vars_list = self.variables[dtype]
            cfg = TYPE_CONFIG[dtype]
            fmt = "<" + cfg["fmt"]
            elem_size = cfg["size"]
            
            # --- A. SCALARS ---
            # Format: [H_S] [Ctx] | [Idx 2B] [Val] | ...
            scalars = [v for v in vars_list if not v.is_array and v.flat_values[0] != 0] # Skip zeros optimization
            
            if scalars:
                current_payload = bytearray()
                pair_size = 2 + elem_size # Idx + Val
                header_len = 3 # H + Ctx
                
                for s in scalars:
                    if (header_len + len(current_payload) + pair_size) > max_size:
                        packets.append(pack_head(cfg["h_scal"]) + current_payload)
                        current_payload = bytearray()
                        
                    current_payload.extend(struct.pack("<H", s.index))
                    current_payload.extend(struct.pack(fmt, s.flat_values[0]))
                
                if len(current_payload) > 0:
                    packets.append(pack_head(cfg["h_scal"]) + current_payload)

            # --- B. ARRAYS ---
            # Format: [H_ARR] [Ctx] | [Idx 2B] [Offset 2B] [Data...]
            arrays = [v for v in vars_list if v.is_array]
            for arr in arrays:
                # Optymalizacja: Pomiń tablice wypełnione zerami (jeśli alokator je zeruje)
                if all(x == 0 for x in arr.flat_values): continue
                
                header_overhead = 3 + 2 + 2 # H(2)+Ctx(1) + Idx(2)+Offset(2)
                max_data_bytes = max_size - header_overhead
                elems_per_pkt = max_data_bytes // elem_size
                
                if elems_per_pkt < 1:
                    print(f"[ERROR] MTU too small for array type {dtype.name}")
                    continue

                total_vals = len(arr.flat_values)
                
                # Chunking based on offset
                for i in range(0, total_vals, elems_per_pkt):
                    chunk = arr.flat_values[i : i + elems_per_pkt]
                    offset = i
                    
                    pkt = bytearray()
                    pkt.extend(pack_head(cfg["h_arr"])) # Header + Ctx
                    pkt.extend(struct.pack("<H", arr.index))
                    pkt.extend(struct.pack("<H", offset))
                    
                    for val in chunk:
                        pkt.extend(struct.pack(fmt, val))
                        
                    packets.append(pkt)

        return packets

# =============================================================================
# PRZYKŁAD UŻYCIA
# =============================================================================
if __name__ == "__main__":
    # Context 0: Global Variables
    mem_glob = EmulatorMemory(context_id=0)
    mem_glob.add("SetPoint", DataTypes.DATA_F, 22.5)
    mem_glob.add("PID_Params", DataTypes.DATA_F, [1.0, 0.1, 0.05], dims=[3])
    
    print("\n--- GLOBAL MEMORY PACKETS ---")
    pkts = mem_glob.get_dump_bytes(max_size=64)
    for p in pkts:
        print(p.hex().upper())

    mem_blocks = EmulatorMemory(context_id=1)
    mem_blocks.add("Timer1_ET", DataTypes.DATA_UI32, 0)
    
    print("\n--- BLOCK MEMORY PACKETS ---")
    pkts_blk = mem_blocks.get_dump_bytes()
    for p in pkts_blk:
        print(p.hex().upper())