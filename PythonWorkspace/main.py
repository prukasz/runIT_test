from Enums import DataTypes, BlockTypes
from BlockStorage import BlocksStorage
from GlobalStorage import GlobalStorage
from GlobalReferences import Global as G
from FullDump import FullDump
from BlockMath import BlockMath
from BlockLogic import BlockLogic
from BlockSetGlobal import BlockSetGlobal
from BlockFor import BlockFor, ForCondition, ForOperator



def main():
    # 1. Init
    var_store = GlobalStorage()
    blk_store = BlocksStorage()
    G.set_store(var_store)

    var_store.add_scalar("value A", DataTypes.DATA_I16, 0.0)
    var_store.add_scalar("value B", DataTypes.DATA_I16, 200)
    var_store.add_array("Array 1", DataTypes.DATA_D, [0]*10, [10])

    b_math_1 = BlockMath(block_idx=0, in_list=[], ref_list=[G("value A").build()],
                        expression="in_1 + 10")
    
    b_math_2 = BlockMath(block_idx=1, in_list=[], ref_list=[G("value B").build()],
                        expression="in_1 + 5")
    
    blk_store.add_block(b_math_1)
    blk_store.add_block(b_math_2)

    b_set_A = BlockSetGlobal(block_idx=2, target_ref=G("value A").build())
    b_set_B = BlockSetGlobal(block_idx=3, target_ref=G("value B").build())

    blk_store.add_block(b_set_A)
    blk_store.add_block(b_set_B)
    b_math_1.connect(1, b_set_A, 0)
    b_math_2.connect(1, b_set_B, 0)

    block_cmp = BlockLogic(block_idx=4, in_list=[], ref_list= [G("value A").build(),G("value B").build() ],  expression="in_1<in_2")
    blk_store.add_block(block_cmp)

    block_for = BlockFor(block_idx=5, chain_len=2, start=0 ,limit=10000, step=1, operator=ForOperator.ADD, condition=ForCondition.LT)
    blk_store.add_block(block_for)
    block_cmp.connect(0, block_for, 0)

    b_math_3 = BlockMath(block_idx=6, in_list=[], ref_list=[G("Array 1").build()],
                        expression="in_1 + 5")
    blk_store.add_block(b_math_3)
    block_for.connect(0, b_math_3, 0)
    b_set_ARR = BlockSetGlobal(block_idx=7, target_ref=G("Array 1").build())
    blk_store.add_block(b_set_ARR)
    b_math_3.connect(1, b_set_ARR, 0)

    filename = "test.txt"
    dumper = FullDump(var_store, blk_store)
    with open(filename, "w") as f:
        dumper.write(f)
    
    print(f"Gotowe: {filename}")

if __name__ == "__main__":
    main()