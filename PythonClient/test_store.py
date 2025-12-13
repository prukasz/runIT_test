from Variables import VariablesStore
from Enums import DataTypes, BlockTypes
from References import Ref
from Blocks import BlockHandle, QConnection
from BlockStorage import BlocksStore
from FullDump import FullDump

def main():
    print("=== GENERATING FULL SYSTEM DUMP (4 BLOCKS) ===")

    # ---------------------------------------------------------
    # 1. SETUP VARIABLES (Shared Memory)
    # ---------------------------------------------------------
    var_store = VariablesStore()
    
    # Create some data to reference
    var_store.add_scalar("sys_tick",   DataTypes.DATA_UI32, 1000)
    var_store.add_scalar("gain_A",     DataTypes.DATA_F,    1.5)
    var_store.add_scalar("nie_wiem",     DataTypes.DATA_I32, -2137)
    var_store.add_scalar("gain_B",     DataTypes.DATA_F,    -0.5)
    var_store.add_array("sensor_raw",  DataTypes.DATA_UI16, [10, 20, 30, 40], [4])
    var_store.add_array("calib_map",   DataTypes.DATA_F,    [0.1]*16, [4, 4])

    # ---------------------------------------------------------
    # 2. SETUP BLOCKS & CONNECTIONS
    # ---------------------------------------------------------
    blk_store = BlocksStore()

    # --- BLOCK 0: Source ---
    # Connection: Output 0 -> Connects to Block 3, Input 0
    # Refs: sys_tick, sensor_raw[0]
    conn_0 = QConnection(target_blocks_id_list=[3], target_inputs_list=[0])
    
    ref0_a = Ref("nie_wiem").build(var_store)
    ref0_b = Ref("sensor_raw")[0].build(var_store)

    blk_0 = BlockHandle(
        block_id=0,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_UI8],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_0],
        global_reference=[ref0_a, ref0_b]
    )

    # --- BLOCK 1: Intermediate A ---
    # Connection: Output 0 -> Connects to Block 2, Input 0
    # Refs: gain_A, calib_map[0,0]
    conn_1 = QConnection(target_blocks_id_list=[2], target_inputs_list=[0])
    
    ref1_a = Ref("gain_A").build(var_store)
    ref1_b = Ref("calib_map")[0, 0].build(var_store)

    blk_1 = BlockHandle(
        block_id=1,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_UI8],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_1],
        global_reference=[ref1_a, ref1_b]
    )

    # --- BLOCK 2: Intermediate B ---
    # Connection: Output 0 -> Connects to Block 3, Input 1
    # Refs: gain_B, sensor_raw[1]
    conn_2 = QConnection(target_blocks_id_list=[3], target_inputs_list=[1])
    
    ref2_a = Ref("gain_B").build(var_store)
    ref2_b = Ref("sensor_raw")[1].build(var_store)

    blk_2 = BlockHandle(
        block_id=2,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_F],     # Input from Block 1
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_2],
        global_reference=[ref2_a, ref2_b]
    )

    # --- BLOCK 3: Sink / Final ---
    # Connection: None (End of chain)
    # Inputs: Comes from Block 0 and Block 2
    # Refs: sys_tick, calib_map[2,2]
    conn_3 = None # No output connection
    
    ref3_a = Ref("sys_tick").build(var_store)
    ref3_b = Ref("calib_map")[2, 2].build(var_store)

    blk_3 = BlockHandle(
        block_id=3,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_F, DataTypes.DATA_F], # Inputs from 0 & 2
        q_data_type_table=[DataTypes.DATA_UI32],
        q_connections_table=[conn_3],
        global_reference=[ref3_a, ref3_b]
    )

    # ---------------------------------------------------------
    # 3. ADD TO STORE & DUMP
    # ---------------------------------------------------------
    # Order of addition doesn't matter for dump (it sorts by ID), 
    # but logically these are our 4 blocks.
    blk_store.add_block(blk_0)
    blk_store.add_block(blk_1)
    blk_store.add_block(blk_2)
    blk_store.add_block(blk_3)

    filename = "test.txt"
    dumper = FullDump(var_store, blk_store)
    
    with open(filename, "w") as f:
        dumper.write(f)

    print(f"\n[SUCCESS] Dumped 4 blocks to '{filename}'.")
    print("-" * 40)
    # Preview the file content
    with open(filename, "r") as f:
        print(f.read())
    print("-" * 40)

if __name__ == "__main__":
    main()