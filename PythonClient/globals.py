from dataclasses import dataclass, field
from typing import List, Optional
from Variables import DataStore, ScalarItem, ArrayItem 

@dataclass
class Global_reference:
    target_alias: str
    static_idx: List[int] # Expects list of 3 ints: [x, y, z]
    reference_idx0: Optional['Global_reference'] = field(default=None)
    reference_idx1: Optional['Global_reference'] = field(default=None)
    reference_idx2: Optional['Global_reference'] = field(default=None)

class Ref:
    """
    Builder class for convenient syntax: Ref("name")[idx, Ref("child")]
    """
    def __init__(self, alias: str):
        self.alias = alias
        self._user_indices: List[int] = []
        self._children: List['Ref'] = []

    def __getitem__(self, items) -> 'Ref':
        # Normalize single item to tuple
        if not isinstance(items, tuple):
            items = (items,)

        for item in items:
            if isinstance(item, int):
                self._user_indices.append(item)
            elif isinstance(item, Ref):
                self._children.append(item)
            elif isinstance(item, str):
                self._children.append(Ref(item)) # Auto-convert string to Ref
            else:
                raise ValueError(f"Unknown item type in Ref: {type(item)}")
        return self

    def build(self, store: DataStore) -> Global_reference:
        """
        Builds the final Global_reference, enforcing FF padding rules 
        based on the target type found in the DataStore.
        """
        # 1. Lookup the target
        target_item = store.get_item(self.alias)
        if not target_item:
            print(f"[WARNING] Reference created to unknown alias: '{self.alias}'. Defaults applied.")
            # Default fallback if alias not found (safe mode)
            final_indices = [0, 255, 255] 
        else:
            # 2. Apply Type-Specific Index Rules
            final_indices = [255, 255, 255] # Start with all invalid/padded

            if isinstance(target_item, ScalarItem):
                # Scalars must have all 255. We ignore user indices.
                pass 

            elif isinstance(target_item, ArrayItem):
                # Fill slots corresponding to array dimensions
                num_dims = len(target_item.dims)
                for i in range(num_dims):
                    if i < len(self._user_indices):
                        final_indices[i] = self._user_indices[i]
                    else:
                        # If array is 2D but user provided 1 index, default 2nd to 0
                        final_indices[i] = 0

        # 3. Recursively build children
        ref0 = self._children[0].build(store) if len(self._children) > 0 else None
        ref1 = self._children[1].build(store) if len(self._children) > 1 else None
        ref2 = self._children[2].build(store) if len(self._children) > 2 else None

        return Global_reference(
            target_alias=self.alias,
            static_idx=final_indices,
            reference_idx0=ref0,
            reference_idx1=ref1,
            reference_idx2=ref2
        )