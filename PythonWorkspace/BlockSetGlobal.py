from BlockBase import BlockBase
from Enums import DataTypes, BlockTypes
from GlobalReferences import Global_reference, Global
from GlobalStorage import ArrayItem
from typing import Optional

class BlockSetGlobal(BlockBase):
    def __init__(self, block_idx: int,target_ref: Global_reference, source_ref: Global_reference = None):
        super().__init__(block_idx, BlockTypes.BLOCK_SET_GLOBAL)
        # autodetect input type 
        val_type = DataTypes(target_ref.target_type_id)
        self.in_data_type_table.append(val_type)
        self.q_data_type_table.append(DataTypes.DATA_B)

        self.add_global_connection(1, target_ref)
        if source_ref is not None:
            self.add_global_connection(0, source_ref)
        
        store = Global._store 
        if store is None:
             raise RuntimeError("BlockSetGlobal: Ref store not linked! Call Ref.set_store(store) first.")
        
        item = store.get_item(target_ref.target_alias)
        if item is None:
             raise ValueError(f"BlockSetGlobal: Variable '{target_ref.target_alias}' not found in storage.")
        if source_ref is not None:
            item = store.get_item(source_ref.target_alias)
            if item is None:
                raise ValueError(f"BlockSetGlobal: Variable '{source_ref.target_alias}' not found in storage.")

        if isinstance(item, ArrayItem):
            for _ in range(len(item.dims)):
                self.in_data_type_table.append(DataTypes.DATA_UI8)

        self.__post_init__()

    def get_extra_data(self) -> str:
        return ""