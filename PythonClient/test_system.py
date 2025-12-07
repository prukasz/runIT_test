from Sender.reference_test.Variables import DataStore
from globals import Ref, Global_reference

# ==========================================
# HELPER FUNCTIONS
# ==========================================

def fmt_hex(indices):
    """Visual helper: Prints 255 as 'FF'."""
    return [f"0xFF" if x == 255 else f"{x}" for x in indices]

def verify_indices(test_name, ref_obj, expected, check_child_alias=None):
    """Compares actual indices and optionally checks if a child reference exists."""
    actual = ref_obj.static_idx
    indices_match = (actual == expected)
    
    child_match = True
    if check_child_alias:
        if not ref_obj.reference_idx0 or ref_obj.reference_idx0.target_alias != check_child_alias:
            child_match = False
            print(f"       [ERR] Child mismatch. Expected {check_child_alias}")

    status = "PASS" if indices_match and child_match else "FAIL"
    
    print(f"[{status}] {test_name}")
    print(f"       Alias:    {ref_obj.target_alias}")
    print(f"       Result:   {fmt_hex(actual)}")
    print(f"       Expected: {fmt_hex(expected)}")
    if check_child_alias:
        child_name = ref_obj.reference_idx0.target_alias if ref_obj.reference_idx0 else "None"
        print(f"       Child:    Found '{child_name}' (Expected '{check_child_alias}')")
    print("-" * 60)

def visualize_tree(node: Global_reference, depth=0):
    """Visualizes the nested reference structure."""
    indent = "    " * depth
    prefix = "└── " if depth > 0 else "ROOT: "
    print(f"{indent}{prefix}{node.target_alias} {fmt_hex(node.static_idx)}")
    
    if node.reference_idx0: visualize_tree(node.reference_idx0, depth + 1)
    if node.reference_idx1: visualize_tree(node.reference_idx1, depth + 1)
    if node.reference_idx2: visualize_tree(node.reference_idx2, depth + 1)

# ==========================================
# MAIN SCENARIO
# ==========================================

def run_generic_scenario():
    # 1. SETUP MEMORY
    store = DataStore()

    # --- DEFINE DATA STRUCTURES ---
    store.add_scalar("scalar_var_nr1", "DATA_F", 1.0)
    store.add_array("arr_1d_nr1",      "DATA_UI8", [0]*10, dims=[10])
    store.add_array("arr_2d_nr1",      "DATA_UI8", [0]*64, dims=[8, 8])
    store.add_array("arr_3d_nr1",      "DATA_UI8", [0]*125, dims=[5, 5, 5])

    print("\n============================================================")
    print("      TEST: INDEX OVERWRITE PROTECTION (AUTO-MASKING)")
    print("============================================================\n")

    # TEST 1: Scalar Logic
    ref_scalar = Ref("scalar_var_nr1")[0, 100, 50].build(store)
    verify_indices("SCALAR PROTECTION", ref_scalar, [255, 255, 255])

    # TEST 2: 1D Array Logic
    ref_1d = Ref("arr_1d_nr1")[4, 99, 99].build(store)
    verify_indices("1D ARRAY MASKING", ref_1d, [4, 255, 255])

    # TEST 3: 2D Array Logic
    ref_2d = Ref("arr_2d_nr1")[2, 6, 99].build(store)
    verify_indices("2D ARRAY MASKING", ref_2d, [2, 6, 255])

    # TEST 4: 3D Array Logic
    ref_3d = Ref("arr_3d_nr1")[1, 2, 3].build(store)
    verify_indices("3D ARRAY NO MASKING", ref_3d, [1, 2, 3])

    print("\n============================================================")
    print("      TEST: MIXED INDICES AND REFERENCES")
    print("============================================================\n")

    # -------------------------------------------------------------
    # TEST 6: Mixing Static Ints and References
    # -------------------------------------------------------------
    # Scenario: 
    #   We access a 2D Array (8x8).
    #   We provide ONE static index: 3 (Row).
    #   We provide ONE reference: "scalar_var_nr1".
    #
    # Logic Interpretation:
    #   1. The static index '3' fills the first dimension.
    #   2. The second dimension defaults to '0' (since only 1 int provided).
    #   3. The third dimension is masked to FF.
    #   4. The "scalar_var_nr1" is attached as a child node.
    
    print(">> Constructing: Ref('arr_2d_nr1')[ 3, Ref('scalar_var_nr1') ]")
    
    mixed_ref = Ref("arr_2d_nr1")[
        3,                      # Static Index (Row)
        Ref("scalar_var_nr1")   # Reference (Dynamic Col or Dependency)
    ].build(store)

    verify_indices(
        test_name="MIXED STATIC + REF",
        ref_obj=mixed_ref,
        expected=[3, 0, 255],   # [Row=3, Col=0 (Default), Masked]
        check_child_alias="scalar_var_nr1"
    )

    print("\n============================================================")
    print("      TEST: DEEP RECURSION VISUALIZATION")
    print("============================================================\n")

    # TEST 5: Nested Structure
    deep_ref = Ref("arr_3d_nr1")[
        0, 0, 0,
        Ref("arr_2d_nr1")[
            1, 1,
            Ref("arr_1d_nr1")[
                5,
                Ref("scalar_var_nr1")
            ]
        ]
    ].build(store)

    visualize_tree(deep_ref)

if __name__ == "__main__":
    run_generic_scenario()