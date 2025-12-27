from typing import List, Any, Dict, Optional, Union
from collections import defaultdict
from dataclasses import dataclass, field
import struct
from Enums import DataTypes, Headers, Order

# ==========================================
# CONFIGURATION
# ==========================================

TYPE_LIMITS_MAP = {
    DataTypes.DATA_UI8:  {"limits": (0, 255)},
    DataTypes.DATA_UI16: {"limits": (0, 65535)},
    DataTypes.DATA_UI32: {"limits": (0, 4294967295)},
    DataTypes.DATA_I8:   {"limits": (-128, 127)},
    DataTypes.DATA_I16:  {"limits": (-32768, 32767)},
    DataTypes.DATA_I32:  {"limits": (-2147483648, 2147483647)},
    DataTypes.DATA_F:    {"limits": None},
    DataTypes.DATA_D:    {"limits": None},
    DataTypes.DATA_B:    {"limits": (0, 1)},
}

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

# ==========================================
# DATA CLASSES
# ==========================================

@dataclass
class ScalarItem:
    alias: str
    dtype: DataTypes
    value: Any
    index: int = field(init=False)

@dataclass
class ArrayItem:
    alias: str
    dtype: DataTypes
    values: Any
    dims: List[int]
    index: int = field(init=False)
    start_index: int = 0

# ==========================================
# MANAGER CLASS
# ==========================================

class GlobalStorage:
    def __init__(self):
        self.arrays: Dict[DataTypes, List[ArrayItem]] = defaultdict(list)
        self.scalars: Dict[DataTypes, List[ScalarItem]] = defaultdict(list)
        self.alias_map = {}

    def _is_type_valid(self, dtype: DataTypes) -> bool:
        if dtype not in TYPE_LIMITS_MAP:
            print(f"[ERROR] Type '{dtype}' is not allowed or configured.")
            return False
        return True

    def is_alias_available(self, alias: str) -> bool:
        if alias in self.alias_map:
            print(f"[ERROR] Alias '{alias}' already exists")
            return False
        return True

    def _flatten_input(self, values: Any) -> List[Any]:
        if not isinstance(values, list):
            return [values]
        out = []
        for v in values:
            out.extend(self._flatten_input(v))
        return out

    def _clamp_and_cast(self, value: Any, dtype: DataTypes) -> Any:
        if isinstance(value, list):
            return [self._clamp_and_cast(v, dtype) for v in value]

        settings = TYPE_LIMITS_MAP[dtype]
        
        if dtype == DataTypes.DATA_B:
            return 1 if value else 0
        
        if settings["limits"] is None:
            try: return float(value)
            except: return 0.0

        min_v, max_v = settings["limits"]
        try:
            val_int = int(value)
            return max(min_v, min(val_int, max_v))
        except:
            return min_v

    def recalculate_indices(self):
        # 1. Recalculate Scalars
        for dtype, items in self.scalars.items():
            for i, item in enumerate(items):
                item.index = i

        # 2. Recalculate Arrays
        for dtype, items in self.arrays.items():      
            by_dim = defaultdict(list)
            for item in items:
                by_dim[len(item.dims)].append(item)
            
            current_index = 0
            for dim_count in sorted(by_dim.keys()):
                for item in by_dim[dim_count]:
                    item.index = current_index
                    current_index += 1

    def add_scalar(self, alias: str, dtype: DataTypes, value: Any) -> Optional[ScalarItem]:
        if not self._is_type_valid(dtype): return None
        if not self.is_alias_available(alias): return None
        
        safe_val = self._clamp_and_cast(value, dtype)
        item = ScalarItem(alias=alias, dtype=dtype, value=safe_val)
        
        self.scalars[dtype].append(item)
        self.alias_map[alias] = item
        
        self.recalculate_indices()
        return item

    def add_array(self, alias: str, dtype: DataTypes, values: Any, dims: List[int]) -> Optional[ArrayItem]:
        if not self._is_type_valid(dtype): return None
        if not self.is_alias_available(alias): return None

        total_size = 1
        for d in dims: total_size *= d

        flat_raw = self._flatten_input(values)
        if len(flat_raw) < total_size:
            flat_raw.extend([0] * (total_size - len(flat_raw)))
        elif len(flat_raw) > total_size:
            flat_raw = flat_raw[:total_size]

        safe_vals = [self._clamp_and_cast(v, dtype) for v in flat_raw]

        item = ArrayItem(alias=alias, dtype=dtype, values=safe_vals, dims=dims)
        
        self.arrays[dtype].append(item)
        self.alias_map[alias] = item
        
        self.recalculate_indices()
        return item

    def get_item(self, alias: str) -> Union[ScalarItem, ArrayItem, None]:
        return self.alias_map.get(alias)

    def remove_item(self, alias: str) -> bool:
        item = self.alias_map.get(alias)
        if not item: return False

        if isinstance(item, ScalarItem):
            self.scalars[item.dtype].remove(item)
        elif isinstance(item, ArrayItem):
            self.arrays[item.dtype].remove(item)
        else:
            return False

        del self.alias_map[alias]
        self.recalculate_indices()
        return True

    def update_scalar_value(self, alias: str, new_value: Any) -> bool:
        item = self.alias_map.get(alias)
        if not item or not isinstance(item, ScalarItem): return False
        item.value = self._clamp_and_cast(new_value, item.dtype)
        return True

    def update_array_values(self, alias: str, new_values: Any) -> bool:
        item = self.alias_map.get(alias)
        if not item or not isinstance(item, ArrayItem): return False
        flat_raw = self._flatten_input(new_values)
        current_len = len(item.values)
        if len(flat_raw) < current_len:
             flat_raw.extend([0] * (current_len - len(flat_raw)))
        item.values = [self._clamp_and_cast(v, item.dtype) for v in flat_raw[:current_len]]
        return True

    # ==========================================
    # NEW DUMP METHODS (Returns String)
    # ==========================================

    def get_dump_list_string(self) -> str:
        """Generates the variable declaration section string."""
        lines = []
        
        # 1. Start Header
        lines.append(f"{Order.ORD_START_BYTES.value:04X}")

        # 2. Variable Counts
        counts = [struct.pack("<B", len(self.scalars[t])).hex().upper() for t in DataTypes]
        lines.append(f"{Order.ORD_H_VARIABLES.value:04X} {' '.join(counts)}")

        # 3. Array Definitions (Grouped by Dims)
        all_arrays = []
        for dtype in DataTypes:
            all_arrays.extend(self.arrays[dtype])
        
        grouped = defaultdict(list)
        for arr in all_arrays:
            grouped[len(arr.dims)].append(arr)

        for dim_count in sorted(grouped.keys()):
            row = [f"FF0{dim_count}"]
            for arr in grouped[dim_count]:
                row.append(struct.pack("<B", arr.dtype.value).hex().upper())
                for d in arr.dims:
                    row.append(struct.pack("<B", d).hex().upper())
            lines.append(" ".join(row))

        # 4. End Header
        lines.append(f"{Order.ORD_PARSE_VARIABLES.value:04X}")
        
        return "\n".join(lines)

    def get_dump_content_string(self) -> str:
        """Generates the content/values section string."""
        lines = []

        # Helper for local packing
        def _pack_val(dtype: DataTypes, value: Any) -> str:
            fmt = DUMP_SETTINGS_MAP[dtype]["fmt"]
            return struct.pack(fmt, value).hex().upper()

        # 1. Arrays (Skip all-zero arrays)
        for dtype in DataTypes:
            for arr in self.arrays[dtype]:
                if all(v == 0 for v in arr.values):
                    continue

                h_table = DUMP_SETTINGS_MAP[dtype]["h_table"]
                
                # Header + Index + StartIndex
                row = [
                    f"{h_table.value:04X}",
                    struct.pack("<B", arr.index).hex().upper(),
                    struct.pack("<H", arr.start_index).hex().upper()
                ]
                
                # Values
                for v in arr.values:
                    row.append(_pack_val(arr.dtype, v))
                
                lines.append(" ".join(row))

        # 2. Scalars (Skip zero values)
        for dtype in DataTypes:
            scalars = self.scalars[dtype]
            active_scalars = [s for s in scalars if s.value != 0]

            if not active_scalars:
                continue

            h_scalar = DUMP_SETTINGS_MAP[dtype]["h_scalar"]
            
            # Header
            row = [f"{h_scalar.value:04X}"]
            
            # Index + Value pairs
            for s in active_scalars:
                row.append(struct.pack("<B", s.index).hex().upper())
                row.append(_pack_val(s.dtype, s.value))
            
            lines.append(" ".join(row))
            
        return "\n".join(lines)