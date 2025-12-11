import sys
from Enums import DataTypes
from Variables import VariablesStore
from DumpVariables import VariablesDump
from References import Ref
from MathExpressionBuilder import MathExpression

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
    
    dumper = VariablesDump(store)
    with open(filename, "w") as f:
        dumper.write(f)
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

if __name__ == "__main__":
    main()