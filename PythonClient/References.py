from dataclasses import dataclass, field
from typing import List, Optional, Union
from Variables import VariablesStore, ScalarItem, ArrayItem

@dataclass
class Global_reference:
    target_alias: str
    target_type_id: int     # Enum Value (0-8)
    target_index: int       # Variable Index
    static_idx: List[int]   # [x, y, z]
    
    # Children references
    reference_idx0: Optional['Global_reference'] = field(default=None)
    reference_idx1: Optional['Global_reference'] = field(default=None)
    reference_idx2: Optional['Global_reference'] = field(default=None)

    def __str__(self):
        """
        Returns Hex String Format: TT II S0 S1 S2 R0 R1 R2
        """
        parts = []
        
        # 1. Type and Index
        parts.append(f"{self.target_type_id:02X}")
        parts.append(f"{self.target_index:02X}")
        
        # 2. Static Indices (S0 S1 S2)
        parts.append(f"{self.static_idx[0]:02X}")
        parts.append(f"{self.static_idx[1]:02X}")
        parts.append(f"{self.static_idx[2]:02X}")

        # 3. Reference Flags/Content (R0 R1 R2)
        def fmt_ref(child_ref):
            if child_ref is not None:
                return str(child_ref)
            else:
                return "FF"

        parts.append(fmt_ref(self.reference_idx0))
        parts.append(fmt_ref(self.reference_idx1))
        parts.append(fmt_ref(self.reference_idx2))
        
        return " ".join(parts)

class Ref:
    def __init__(self, alias: str):
        self.alias = alias
        self._args: List[Union[int, 'Ref']] = []

    def __getitem__(self, items) -> 'Ref':
        if not isinstance(items, tuple): items = (items,)
        for item in items:
            if isinstance(item, (int, Ref)): self._args.append(item)
            elif isinstance(item, str): self._args.append(Ref(item))
            else: raise ValueError(f"Unknown item type: {type(item)}")
        return self

    def build(self, store: VariablesStore) -> Global_reference:
        target_item = store.get_item(self.alias)
        
        # Defaults
        t_id = 0
        t_idx = 0
        final_static = [255, 255, 255]
        child_refs = [None, None, None]

        if not target_item:
            print(f"[WARNING] Unknown alias '{self.alias}'. Defaults used.")
            final_static = [0, 255, 255]
        else:
            t_id = target_item.dtype.value
            t_idx = target_item.index

            # Determine how many dimensions actually exist
            num_valid_dims = 0
            if isinstance(target_item, ArrayItem):
                num_valid_dims = len(target_item.dims)
            
            # Iterate strictly over the 3 available slots (X, Y, Z)
            for i in range(3):
                # 1. User provided a custom argument for this slot
                if i < len(self._args):
                    arg = self._args[i]
                    
                    if isinstance(arg, int):
                        # Custom Static Index
                        final_static[i] = arg
                        
                    elif isinstance(arg, Ref):
                        # Dynamic Reference
                        final_static[i] = 0 # Placeholder for dynamic logic
                        child_refs[i] = arg.build(store)
                
                # 2. No user input -> Apply defaults based on variable type
                else:
                    if i < num_valid_dims:
                        # Slot is a valid array dimension -> Default to 0
                        final_static[i] = 0
                    else:
                        # Slot is unused (Scalar, or beyond array dims) -> Default to 255 (FF)
                        final_static[i] = 255

        return Global_reference(
            target_alias=self.alias,
            target_type_id=t_id,
            target_index=t_idx,
            static_idx=final_static,
            reference_idx0=child_refs[0],
            reference_idx1=child_refs[1],
            reference_idx2=child_refs[2]
        )