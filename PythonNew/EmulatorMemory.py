import struct
from typing import List, Any, Dict, Optional, Union
from collections import defaultdict
from dataclasses import dataclass, field
from Enums import emu_types_t, Headers, TYPE_CONFIG




@dataclass
class Variable:
    alias: str
    dtype: emu_types_t
    value: Any          
    dims: List[int]     # [] = Scalar, [x, y] = Array
    
    #Auto compute
    index: int = field(init=False, default=0)
    is_array: bool = field(init=False)
    flat_values: List[Any] = field(init=False)
    
    def __post_init__(self):
        self.is_array = (len(self.dims) > 0)
        
        #Array setup
        if self.is_array:
            self.flat_values = self._flatten(self.value)
            
            #Total elememts in array
            total_elements = 1
            for d in self.dims: total_elements *= d
            
            #Padding or trim
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
# EMULATOR MEMORU CLASS
# =============================================================================

class EmulatorMemory:
    def __init__(self, context_id: int = 0):
        self.context_id = context_id #0 is global #1 is for outputs
        self.variables: Dict[emu_types_t, List[Variable]] = defaultdict(list)
        self.alias_map = {}

    def add(self, alias: str, dtype: emu_types_t, value: Any, dims: List[int] = []) -> bool:
        if alias in self.alias_map:
            print(f"[ERROR] Alias '{alias}' already exists in Ctx {self.context_id}.")
            return False
            
        var = Variable(alias, dtype, value, dims)
        self.variables[dtype].append(var)
        self.alias_map[alias] = var
        return True

    def get_var_index(self, alias: str) -> Optional[int]:
        """Return alias index"""
        self.recalculate_indices() # Upewnij się, że indeksy są aktualne
        if alias in self.alias_map:
            return self.alias_map[alias].index
        return None

    def recalculate_indices(self):
        """Sort alias by idx. First scalars, then arrays"""
        for dtype in emu_types_t:
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

    def get_dump_bytes(self, max_size: int = 250) -> List[bytes]:
        """Dump Hex packets"""
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

        for dtype in emu_types_t:
            vars_list = self.variables[dtype]
            
            #Total elements (Pool Size)
            total_elems = sum(len(v.flat_values) for v in vars_list)
            var_counts.extend(struct.pack("<H", total_elems))
            
            #Instance count (Instances Size)
            inst_counts.extend(struct.pack("<H", len(vars_list)))
            
            #Extra Dims Bytes (Arena Size needed for dim_sizes array)
            dims_bytes = sum(len(v.dims) for v in vars_list if v.is_array)
            extra_dims.extend(struct.pack("<H", dims_bytes))

        config_payload = var_counts + inst_counts + extra_dims 
        packets.append(pack_head(Headers.VAR_H_SIZES) + config_payload)

        # ---------------------------------------------------------
        # SCALAR COUNT PACKET (Header: 0xFF01)
        # Format: [H] [Ctx] [Cnt_UI8] [Cnt_UI16] ...
        # ---------------------------------------------------------
        scalar_payload = bytearray()
        for dtype in emu_types_t:
            cnt = len([v for v in self.variables[dtype] if not v.is_array])
            scalar_payload.extend(struct.pack("<H", cnt))
            
        packets.append(pack_head(Headers.VAR_H_SCALAR_CNT) + scalar_payload)

        # ---------------------------------------------------------
        # ARRAY DEFINITION PACKETS (Header: 0xFF02)
        # Format: [H] [Ctx] | [DimsCnt] [Type] [D0] [D1]... | ...
        # ---------------------------------------------------------
        arr_buffer = bytearray()
        curr_buf_len = 3 #Header(2) + Ctx(1)
        
        for dtype in emu_types_t:
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
        # DATA PACKETS (VALUES)
        # ---------------------------------------------------------
        for dtype in emu_types_t:
            vars_list = self.variables[dtype]
            cfg = TYPE_CONFIG[dtype]
            fmt = "<" + cfg["fmt"]
            elem_size = cfg["size"]
            
            # ---SCALARS---
            #Format: [H_S] [Ctx] | [Idx 2B] [Val] | ...
            scalars = [v for v in vars_list if not v.is_array and v.flat_values[0] != 0] # Skip zeros
            
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

            # ---ARRAYS---
            #Format: [H_ARR] [Ctx] | [Idx 2B] [Offset 2B] [Data...]
            arrays = [v for v in vars_list if v.is_array]
            for arr in arrays:
                if all(x == 0 for x in arr.flat_values): continue # Skip empty arrays
                
                header_overhead = 3 + 2 + 2 # H(2)+Ctx(1) + Idx(2)+Offset(2)
                max_data_bytes = max_size - header_overhead
                elems_per_pkt = max_data_bytes // elem_size
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
    mem_glob.add("SetPoint", emu_types_t.DATA_F, 22.5)
    mem_glob.add("PID_Params", emu_types_t.DATA_F, [1.0, 0.1, 0.05], dims=[3])
    
    print("\n--- GLOBAL MEMORY PACKETS ---")
    pkts = mem_glob.get_dump_bytes(max_size=64)
    for p in pkts:
        print(p.hex().upper())

    mem_blocks = EmulatorMemory(context_id=1)
    mem_blocks.add("Timer1_ET", emu_types_t.DATA_UI32, 0)
    
    print("\n--- BLOCK MEMORY PACKETS ---")
    pkts_blk = mem_blocks.get_dump_bytes()
    for p in pkts_blk:
        print(p.hex().upper())