import struct
from typing import List, Any, TextIO
from collections import defaultdict
from Enums import DataTypes, Headers, Order

DUMP_SETTINGS_MAP = {
    DataTypes.DATA_UI8:  {"fmt": "<B", "h_table": Headers.H_TABLE_UI8,  "h_scalar": Headers.H_SCALAR_UI8},
    DataTypes.DATA_UI16: {"fmt": "<H", "h_table": Headers.H_TABLE_UI16, "h_scalar": Headers.H_SCALAR_UI16},
    DataTypes.DATA_UI32: {"fmt": "<I", "h_table": Headers.H_TABLE_UI32, "h_scalar": Headers.H_SCALAR_UI32},
    DataTypes.DATA_I8:   {"fmt": "<b", "h_table": Headers.H_TABLE_I8,   "h_scalar": Headers.H_SCALAR_I8},
    DataTypes.DATA_I16:  {"fmt": "<h", "h_table": Headers.H_TABLE_I16,  "h_scalar": Headers.H_SCALAR_I16},
    DataTypes.DATA_I32:  {"fmt": "<i", "h_table": Headers.H_TABLE_I32,  "h_scalar": Headers.H_SCALAR_I32},
    DataTypes.DATA_F:    {"fmt": "<f", "h_table": Headers.H_TABLE_F,    "h_scalar": Headers.H_SCALAR_F},
    DataTypes.DATA_D:    {"fmt": "<d", "h_table": Headers.H_TABLE_D,    "h_scalar": Headers.H_SCALAR_D},
    DataTypes.DATA_B:    {"fmt": "<?", "h_table": Headers.H_TABLE_B,    "h_scalar": Headers.H_SCALAR_B},
}

class VariablesDump:
    def __init__(self, store):
        self.store = store

    def _pack_val(self, dtype: DataTypes, value: Any) -> str:
        fmt = DUMP_SETTINGS_MAP[dtype]["fmt"]
        return struct.pack(fmt, value).hex().upper()

    def dump_variable_list(self, writer: TextIO):
        writer.write(f"{Order.ORD_START_BYTES.value:04X}\n")

        counts = [struct.pack("<B", len(self.store.scalars[t])).hex().upper() for t in DataTypes]
        writer.write(f"{Order.ORD_H_VARIABLES.value:04X} {' '.join(counts)}\n")

        all_arrays = []
        for dtype in DataTypes: 
            all_arrays.extend(self.store.arrays[dtype])
        
        grouped = defaultdict(list)
        for arr in all_arrays: 
            grouped[len(arr.dims)].append(arr)

        for dim_count in sorted(grouped.keys()):
            line = [f"FF0{dim_count}"]
            for arr in grouped[dim_count]:
                line.append(struct.pack("<B", arr.dtype.value).hex().upper())
                for d in arr.dims: 
                    line.append(struct.pack("<B", d).hex().upper())
            writer.write(" ".join(line) + "\n")

        writer.write(f"{Order.ORD_PARSE_VARIABLES.value:04X}\n")

    def dump_variable_content(self, writer: TextIO):
        # 1. Arrays (Skip if all values are zero)
        for dtype in DataTypes:
            for arr in self.store.arrays[dtype]:
                # Check if array is all zeros
                if all(v == 0 for v in arr.values):
                    continue

                h_table = DUMP_SETTINGS_MAP[dtype]["h_table"]
                line = [f"{h_table.value:04X}"]
                line.append(struct.pack("<B", arr.index).hex().upper())
                line.append(struct.pack("<H", arr.start_index).hex().upper())
                
                for v in arr.values:
                    line.append(self._pack_val(arr.dtype, v))
                writer.write(" ".join(line) + "\n")

        # 2. Scalars (Grouped by Header, Skip Zeros)
        for dtype in DataTypes:
            scalars = self.store.scalars[dtype]
            
            # Filter: Get only non-zero scalars
            active_scalars = [s for s in scalars if s.value != 0]

            if not active_scalars:
                continue

            h_scalar = DUMP_SETTINGS_MAP[dtype]["h_scalar"]
            
            # Start Line with Header
            line_parts = [f"{h_scalar.value:04X}"]
            
            # Append Index + Value pairs
            for s in active_scalars:
                line_parts.append(struct.pack("<B", s.index).hex().upper())
                line_parts.append(self._pack_val(s.dtype, s.value))
            
            writer.write(" ".join(line_parts) + "\n")
#git 
    def write(self, writer: TextIO):
        self.dump_variable_list(writer)
        self.dump_variable_content(writer)