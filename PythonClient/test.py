from Enums import DataTypes, BlockTypes
from References import Ref
from Blocks import BlockHandle, QConnection
from BlocksStorage import BlocksStorage
from VariablesStorage import VariablesStorage
from FullDump import FullDump
from MathExpressionBuilder import MathExpression

def main():
    var_store = VariablesStorage()
    blk_store = BlocksStorage()
    Ref.set_store(var_store)
    
    # Define the 3x3 matrix
    var_store.add_scalar("dupa", DataTypes.DATA_D, 2)
    var_store.add_array("matrix_3x3",  DataTypes.DATA_F,    [0.0]*9,  [3, 3])

    # 1. Block to provide the value (99.9)
    expr_val = MathExpression("99.9", block_id=0)
    blk_val = BlockHandle(
        block_idx=0,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_D],
        q_data_type_table=[DataTypes.DATA_D],
        q_connections_table=[QConnection(target_blocks_idx_list=[3], target_inputs_num_list=[0])],
        block_data=expr_val
    )

    # 2. Block to provide Row Index (1)
    expr_row = MathExpression("1", block_id=1)
    blk_row = BlockHandle(
        block_idx=1,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_D],
        q_data_type_table=[DataTypes.DATA_UI8],
        q_connections_table=[QConnection(target_blocks_idx_list=[3], target_inputs_num_list=[1])],
        block_data=expr_row
    )

    # 3. Block to provide Column Index (2)
    expr_col = MathExpression("2", block_id=2)
    blk_3 = BlockHandle(
        block_idx=2,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_D],
        q_data_type_table=[DataTypes.DATA_UI8],
        q_connections_table=[QConnection(target_blocks_idx_list=[3], target_inputs_num_list=[1])],
        block_data=expr_col
    )

    # 4. Global Set Block to write matrix_3x3[row][col] = value
    ref_matrix = Ref("matrix_3x3")[1][Ref("dupa")].build()
    blk_set = BlockHandle(
        block_idx=3,
        block_type=BlockTypes.BOCK_GLOBAL_SET,
        in_data_type_table=[DataTypes.DATA_D, DataTypes.DATA_UI8],
        q_data_type_table=[],
        q_connections_table=[],
        global_reference=[ref_matrix]
    )

    blk_store.add_block(blk_val)
    blk_store.add_block(blk_row)
    blk_store.add_block(blk_3)
    blk_store.add_block(blk_set)

    filename = "test.txt"
    dumper = FullDump(var_store, blk_store)
    
    with open(filename, "w") as f:
        dumper.write(f)

if __name__ == "__main__":
    main()