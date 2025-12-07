from typing import List, Any, Dict, Optional, Union
from collections import defaultdict
from dataclasses import dataclass, field
from Enums import DataTypes

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

class VariablesStore:
    def __init__(self):
        # stored by type: self.arrays[DataTypes.DATA_UI8] = [item0, item1, ...]
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
        """Recursively flattens input for storage and padding."""
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

    # ==========================================
    # CORE INDEXING LOGIC
    # ==========================================

    def recalculate_indices(self):
        """
        [CRITICAL] Recalculates indices to match the Hex Dump Protocol.
        
        Protocol Logic:
        1. Scalars are dumped sequentially per Type.
           -> Index = Order in list.
           
        2. Arrays are dumped grouped by Dimension Count (1D, then 2D, then 3D...).
           -> Index must respect this order.
           -> All 1D arrays of Type T get indices 0..k
           -> All 2D arrays of Type T get indices k+1..m
           -> etc.
        """
        # 1. Recalculate Scalars (Simple sequential)
        for dtype, items in self.scalars.items():
            for i, item in enumerate(items):
                item.index = i

        # 2. Recalculate Arrays (Dimension Priority)
        for dtype, items in self.arrays.items():
            # We must simulate the dump order: Sorted by dimension count
            # Note: We use a stable sort (key only dims) so insertion order is preserved 
            # for arrays of same dimension.
            
            # Temporary list of tuples (original_item, dim_count)
            # We don't sort the main list to preserve user's logical order, 
            # but we assign indices based on the sorted projection.
            
            # Actually, to make it robust, we calculate what the index WOULD be.
            # Count how many arrays of SAME type have LOWER dimension count.
            # Count how many arrays of SAME type have EQUAL dimension count but appear earlier.
            
            # Optimization: Group by dim locally
            by_dim = defaultdict(list)
            for item in items:
                by_dim[len(item.dims)].append(item)
            
            current_index = 0
            # Iterate dimensions in increasing order (1, 2, 3...)
            for dim_count in sorted(by_dim.keys()):
                for item in by_dim[dim_count]:
                    item.index = current_index
                    current_index += 1

    # ==========================================
    # ADDERS
    # ==========================================

    def add_scalar(self, alias: str, dtype: DataTypes, value: Any) -> Optional[ScalarItem]:
        if not self._is_type_valid(dtype): return None
        if not self.is_alias_available(alias): return None
        
        safe_val = self._clamp_and_cast(value, dtype)
        item = ScalarItem(alias=alias, dtype=dtype, value=safe_val)
        
        self.scalars[dtype].append(item)
        self.alias_map[alias] = item
        
        # Always recalc to be safe
        self.recalculate_indices()
        return item

    def add_array(self, alias: str, dtype: DataTypes, values: Any, dims: List[int]) -> Optional[ArrayItem]:
        if not self._is_type_valid(dtype): return None
        if not self.is_alias_available(alias): return None

        # Size calc & Padding
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
        
        # [CRITICAL] Recalculate because adding a 1D array might shift the 
        # indices of existing 2D arrays of the same type.
        self.recalculate_indices()
        return item

    # ==========================================
    # GETTERS & UTILS
    # ==========================================

    def get_item(self, alias: str) -> Union[ScalarItem, ArrayItem, None]:
        return self.alias_map.get(alias)

    def get_export_map(self) -> Dict[str, Dict[str, Any]]:
        export = {}
        for alias, item in self.alias_map.items():
            category = "scalar" if isinstance(item, ScalarItem) else "array"
            export[alias] = {
                "index": item.index,
                "type": item.dtype.name,
                "category": category
            }
        return export

    # ==========================================
    # REMOVAL & UPDATES
    # ==========================================

    def remove_item(self, alias: str) -> bool:
        item = self.alias_map.get(alias)
        if not item:
            print(f"[ERROR] Cannot remove '{alias}'. Alias not found.")
            return False

        if isinstance(item, ScalarItem):
            target_list = self.scalars[item.dtype]
        elif isinstance(item, ArrayItem):
            target_list = self.arrays[item.dtype]
        else:
            return False

        try:
            target_list.remove(item)
            del self.alias_map[alias]
        except ValueError:
            return False

        # Recalculate all indices to fill gaps
        self.recalculate_indices()
            
        print(f"[INFO] Removed '{alias}' and re-indexed.")
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
        
        # Pad or Truncate
        if len(flat_raw) < current_len:
             flat_raw.extend([0] * (current_len - len(flat_raw)))
        
        item.values = [self._clamp_and_cast(v, item.dtype) for v in flat_raw[:current_len]]
        return True