<<<<<<< HEAD
import io
=======
>>>>>>> d9186c7 (python test code)
import sys
from Enums import DataTypes
from Variables import VariablesStore
from DumpVariables import VariablesDump
<<<<<<< HEAD
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
=======
from References import Ref

def main():
    print("=== TEST: DUMP + DEEP NESTED REF HEX ===")

    store = VariablesStore()

    # 1. SETUP VARIABLES
    # ------------------------------------------------
    # A. Shallow Test Variables
    store.add_scalar("sys_offset", DataTypes.DATA_UI8, 2)
    store.add_array("motor_curve", DataTypes.DATA_UI16, [100, 200, 300, 400], [4])

    # B. Deep Nesting Variables
    # ------------------------------------------------
    # LEVEL 3 (Innermost): The initial selector
    # Value is 1.
    store.add_scalar("deep_selector", DataTypes.DATA_UI8, 1)

    # LEVEL 2 (Middle): The lookup table
    # We will access lookup_table[ deep_selector ] -> lookup_table[1]
    # Value at index 1 is '3'. 
    # We must ensure this '3' is a valid index for the next level.
    store.add_array("lookup_table", DataTypes.DATA_UI8, [0, 3, 1, 0], [4])

    # LEVEL 1 (Outermost): The target data
    # We will access target_data[ 3 ]
    # Size must be at least 4.
    store.add_array("target_data", DataTypes.DATA_UI32, [1111, 2222, 3333, 4444], [4])

    print("Variables Created.")

    # 2. DUMP TO FILE
    # ------------------------------------------------
    filename = "valuedump.txt"
    print(f"Dumping variables to '{filename}'...")
>>>>>>> d9186c7 (python test code)
    
    dumper = VariablesDump(store)
    with open(filename, "w") as f:
        dumper.write(f)
<<<<<<< HEAD
=======
    print("Dump Complete.")

    # 3. TEST 1: STANDARD NESTED REFERENCE
    # ------------------------------------------------
    print("\n--- 1. Simple Nested Reference ---")
    # Logic: motor_curve[2]
    ref1 = Ref("motor_curve")[ Ref("sys_offset") ].build(store)
    print(f"Ref: motor_curve[sys_offset]")
    print(f"Hex: {ref1}")

    # 4. TEST 2: DEEP NESTED REFERENCE
    # ------------------------------------------------
    # Scenario: target_data[ lookup_table[ deep_selector ] ]
    # Resolution:
    # 1. deep_selector = 1
    # 2. lookup_table[1] = 3
    # 3. target_data[3] = 4444 (Safe, max index is 3)
    
    print("\n--- 2. Deep Nested Reference ---")
    
    ref2 = Ref("target_data")[ 
        Ref("lookup_table")[ 
            Ref("deep_selector") 
        ] 
    ].build(store)

    print(f"Ref: target_data[ lookup_table[ deep_selector ] ]")
    print(f"Hex: {ref2}")
>>>>>>> d9186c7 (python test code)

if __name__ == "__main__":
    main()