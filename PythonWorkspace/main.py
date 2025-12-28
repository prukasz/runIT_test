from Enums import DataTypes
from BlockStorage import BlocksStorage
from GlobalStorage import GlobalStorage
from GlobalReferences import Global as G
from FullDump import FullDump

# Importy bloków
from BlockLogic import BlockLogic
from BlockSetGlobal import BlockSetGlobal
from BlockFor import BlockFor, ForCondition, ForOperator
from BlockMath import BlockMath


def main():
    # 1. Inicjalizacja
    glob_store = GlobalStorage()
    blk_store = BlocksStorage()
    G.set_store(glob_store)

    glob_store.add_scalar("PRESSURE", DataTypes.DATA_D, 60.0)
    glob_store.add_scalar("LIMIT_VAL", DataTypes.DATA_D, 50.0)
    glob_store.add_scalar("ALARM_LEVEL", DataTypes.DATA_D, 0)
    b_logic = BlockLogic(
        block_idx=0,
        expression="in_1 > in_2",
        in_list=[], 
        global_refs={1: G("PRESSURE").build(), 2: G("LIMIT_VAL").build()}
    )
    blk_store.add_block(b_logic)
    b_for = BlockFor(
        block_idx=1,
        chain_len=2,
        start=0,
        limit=100,
        step=1,
        condition=ForCondition.LT,
        operator=ForOperator.ADD
    )
    blk_store.add_block(b_for)

    b_math = BlockMath(
        block_idx=2,
        expression="in_1 + in_2",
        global_refs={1: G("PRESSURE").build(), 2: G("LIMIT_VAL").build()}
    )
    blk_store.add_block(b_math)

    b_set = BlockSetGlobal(
        block_idx=3,
        target_ref=G("ALARM_LEVEL").build()
    )
    blk_store.add_block(b_set)

    b_logic.connect(0, b_for, 0)
    b_for.connect(0, b_math, 0)
    b_math.connect(1, b_set, 0)

    filename = "test.txt"
    dumper = FullDump(glob_store, blk_store)
    
    with open(filename, "w") as f:
        dumper.write(f)
    
    print(f"Gotowe! Plik '{filename}' wygenerowany.")
    
    # Weryfikacja zawartości pod kątem referencji
    with open(filename, "r") as f:
        print("\n--- Podgląd Referencji w pliku ---")
        for line in f:
            if "#REF" in line:
                print(line.strip())

if __name__ == "__main__":
    main()