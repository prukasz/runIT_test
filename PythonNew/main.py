import sys
from EmulatorMemory import EmulatorMemory, emu_types_t as DataTypes
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from BlockLogic import BlockLogic
from BlockClock import BlockClock
from BlockMath import BlockMath
from BlockCounter import BlockCounter, CounterConfig
from BlockSet import BlockSet
from FullDump import FullDump
from BlockFor import BlockFor, ForCondition, ForOperator


if __name__ == "__main__":
    print("=== SCENARIUSZ: LOGIC -> CLOCK -> COUNTER -> SET (FEEDBACK) ===\n")

    # -------------------------------------------------------------------------
    # 1. SETUP PAMIĘCI
    # -------------------------------------------------------------------------
    Ref.clear_memories()
    
    # Konteksty
    mem_glob = EmulatorMemory(context_id=0) # Zmienne globalne
    mem_blk  = EmulatorMemory(context_id=1) # Wyjścia bloków
    
    Ref.register_memory(mem_glob)
    Ref.register_memory(mem_blk)

    # Zmienne globalne
    mem_glob.add("x", DataTypes.DATA_F, 2) # Zmienna sterująca / licznik pętli
    mem_glob.add("en", DataTypes.DATA_B, True) # Zmienna sterująca
    mem_glob.add("array", DataTypes.DATA_UI32, [1]*100, [100])
    
    # Przeliczenie indeksów globalnych
    mem_glob.recalculate_indices()

    # Storage Bloków
    storage = BlocksStorage(mem_blk)

    # -------------------------------------------------------------------------
    # 2. KONFIGURACJA BLOKÓW
    # -------------------------------------------------------------------------
    
    # --- BLOK 0: LOGIC ---
    # Warunek: "x < 20"
    # Inputs:
    #   1. Ref("x")
    #   2. Const 20.0 (Automatycznie obsłużone przez BlockLogic/Ref)
    blk_logic = BlockLogic(
        block_idx=0,
        storage=storage,
        expression="in_1 < 400", 
        en=Ref("en"),
        connections=[
            Ref("x") # in_1
        ]
    )

    blk_clock = BlockClock(
        block_idx=1,
        storage=storage,
        enable=blk_logic.out[1], 
        period_ms=2000.0,
        width_ms=1000.0
    )


    blk_for = BlockFor(
        block_idx=2,
        storage=storage,
        condition = ForCondition.LT,
        start=0,
        limit=10,
        step=1,
        chain_len=2,
        en=blk_clock.out[0]
    )

    #array[iterator]*2
    blk_math = BlockMath(
        block_idx=3,
        storage=storage,
        expression="in_1*in_2",
        en=blk_for.out[1],
        connections=[Ref("array")[blk_for.out[1]], blk_for.out[1]]
    )


    blk_set2 = BlockSet(
        block_idx=4,
        storage=storage,
        target=Ref("array")[blk_for.out[1]],
        value=blk_math.out[1] 
    )

    


    # -------------------------------------------------------------------------
    # 3. FINALIZACJA
    # -------------------------------------------------------------------------
    
    mem_blk.recalculate_indices()
    storage.recalculate_connection_masks()

    # -------------------------------------------------------------------------
    # 4. GENEROWANIE DUMP
    # -------------------------------------------------------------------------
    
    dump = FullDump(mem_glob, mem_blk, storage)
    
    # Zapis do pliku
    output_filename = "test.txt"
    with open(output_filename, "w") as f:
        dump.write(f)
        
    print(f"\n[SUKCES] Wygenerowano: {output_filename}")
    
    # Podgląd na konsolę (opcjonalnie)
    # dump.write(sys.stdout)