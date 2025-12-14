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
    
    var_store.add_scalar("sys_tick",   DataTypes.DATA_UI32, 1000)
    var_store.add_scalar("gain_A",     DataTypes.DATA_F,    1.5)
    var_store.add_scalar("nie_wiem",     DataTypes.DATA_I32, -2137)
    var_store.add_scalar("gain_B",     DataTypes.DATA_F,    -0.5)
    var_store.add_array("sensor_raw",  DataTypes.DATA_UI16, [10, 20, 30, 40], [4])
    var_store.add_array("calib_map",   DataTypes.DATA_F,    [0.1]*16, [4, 4])

    conn_0 = QConnection(target_blocks_idx_list=[1,3], target_inputs_num_list=[0,1])
    expr = MathExpression("100", block_id=0x0000)
    ref1 = Ref("gain_B").build()
    blk_0 = BlockHandle(
        block_idx=0,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_UI8],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_0],
        global_reference=[ref1],
        block_data = expr
    )

    conn_1 = QConnection(target_blocks_idx_list=[2], target_inputs_num_list=[0])
    expr1 = MathExpression("in_1+2", block_id=0x0001)
    blk_1 = BlockHandle(
        block_idx=1,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_F],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_1],
        global_reference=[],
        block_data=expr1
    )

    conn_2 = QConnection(target_blocks_idx_list=[3], target_inputs_num_list=[0])
    expr2 = MathExpression("in_1+2", block_id=0x0002)
    blk_2 = BlockHandle(
        block_idx=2,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_F],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[conn_2],
        global_reference=[],
        block_data= expr2
    )
    expr3 = MathExpression("in_1+in_2", block_id=0x0003)
    blk_3 = BlockHandle(
        block_idx=3,
        block_type=BlockTypes.BLOCK_MATH,
        in_data_type_table=[DataTypes.DATA_D, DataTypes.DATA_D],
        q_data_type_table=[DataTypes.DATA_F],
        q_connections_table=[None],
        global_reference=[],
        block_data=expr3
    )

    blk_store.add_block(blk_0)
    blk_store.add_block(blk_1)
    blk_store.add_block(blk_2)
    blk_store.add_block(blk_3)

    filename = "test.txt"
    dumper = FullDump(var_store, blk_store)
    
    with open(filename, "w") as f:
        dumper.write(f)

if __name__ == "__main__":
    main()