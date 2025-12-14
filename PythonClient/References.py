from dataclasses import dataclass, field
from typing import List, Optional, Union
from VariablesStorage import VariablesStorage, ScalarItem, ArrayItem

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
    # 1. Class-level variable to hold the storage
    _store: Optional[VariablesStorage] = None

    @classmethod
    def set_store(cls, store: VariablesStorage):
        """Call this ONCE at the start of your program to link the storage."""
        cls._store = store

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

    # 2. build() now takes NO arguments
    def build(self) -> Global_reference:
        if Ref._store is None:
            raise RuntimeError("You must call Ref.set_store(store) before building references!")
        
        # Use the global store
        store = Ref._store
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

            num_valid_dims = 0
            if isinstance(target_item, ArrayItem):
                num_valid_dims = len(target_item.dims)
            
            for i in range(3):
                if i < len(self._args):
                    arg = self._args[i]
                    if isinstance(arg, int):
                        final_static[i] = arg
                    elif isinstance(arg, Ref):
                        final_static[i] = 0 
                        # Recursive call: Child .build() also uses global store
                        child_refs[i] = arg.build() 
                else:
                    if i < num_valid_dims:
                        final_static[i] = 0
                    else:
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