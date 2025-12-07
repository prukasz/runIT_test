import io
import sys
from Enums import DataTypes
from Variables import VariablesStore
from DumpVariables import VariablesDump
from pprint import pprint

def main():
    store = VariablesStore()
    store.add_array("volume_double", DataTypes.DATA_D, [8]*27, [3,3,3])
    store.add_array("matrix_A", DataTypes.DATA_UI16, [10, 20, 30, 40], [2, 2])
    store.add_array("linear_A", DataTypes.DATA_UI16, [1, 2, 3, 4], [4])
    store.add_array("volume_A", DataTypes.DATA_UI16, [1]*8, [2, 2, 2])
    store.add_array("linear_B", DataTypes.DATA_UI16, [8, 9], [2])
    store.add_scalar("scalarA", DataTypes.DATA_UI16, 10)
    store.add_scalar("scalarB", DataTypes.DATA_UI16, 11)
    store.add_array("linear_C", DataTypes.DATA_UI16, [8, 9], [4])
    store.add_array("linear_Double", DataTypes.DATA_D, [8]*40, [40])

    export  = store.get_export_map()
    pprint(export)
    
    filename = "valuedump.txt"
    print(f"\n[3] Dumping to '{filename}' for manual check...")
    
    dumper = VariablesDump(store)
    with open(filename, "w") as f:
        dumper.write(f)

if __name__ == "__main__":
    main()