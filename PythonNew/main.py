import sys
from EmulatorMemory import EmulatorMemory
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from BlockLogic import BlockLogic
from BlockClock import BlockClock
from BlockMath import BlockMath
from BlockCounter import BlockCounter, CounterConfig
from BlockSet import BlockSet
from FullDump import FullDump
from BlockFor import BlockFor, ForCondition, ForOperator
from BlockSelector import BlockSelector
from Enums import emu_types_t


if __name__ == "__main__":
    print("=== SCENARIUSZ: LOGIC -> CLOCK -> COUNTER -> SET (FEEDBACK) ===\n")

    # -------------------------------------------------------------------------
    # 1. SETUP PAMIĘCI
    # -------------------------------------------------------------------------
    Ref.clear_contexts()
    # Konteksty
    mem_glob = EmulatorMemory(context_id=0) # Zmienne globalne
    mem_blk  = EmulatorMemory(context_id=6) # Wyjścia bloków
    
    Ref.register_context(mem_glob)
    Ref.register_context(mem_blk)

    # Zmienne globalne
    mem_glob.add("selector", emu_types_t.DATA_UI8, 0) # Zmienna sterująca / licznik pętli
    mem_glob.add("en", emu_types_t.DATA_B, True) # Zmienna sterująca
    mem_glob.add("array1", emu_types_t.DATA_UI32, [1]*255, [255])
    mem_glob.add("array2", emu_types_t.DATA_UI32, [2]*255, [255])
    mem_glob.add("array3", emu_types_t.DATA_UI32, [0]*255, [255])
    
    # Przeliczenie indeksów globalnych
    mem_glob.recalculate_indices()

    # Storage Bloków
    storage = BlocksStorage(mem_blk)

    # -------------------------------------------------------------------------
    # 2. KONFIGURACJA BLOKÓW
    # -------------------------------------------------------------------------
   
    clk = BlockClock(
        block_idx=0,
        storage=storage,
        period_ms=2000,
        enable=Ref("en"),
        width_ms=1000
    )
    
    selector = BlockSelector(
        block_idx=1,
        storage=storage,
        selector=Ref("selector"),
        options=[Ref("array1"), Ref("array2"), Ref("array3")]  # Example options
    )
    set = BlockSet(
        block_idx=2,
        storage=storage,
        value=selector.out[0],
        target=Ref("selector")
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