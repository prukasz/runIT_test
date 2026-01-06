import sys
from EmulatorMemory import EmulatorMemory, emu_types_t as DataTypes
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from BlockLogic import BlockLogic
from BlockClock import BlockClock
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
    mem_glob.add("x", DataTypes.DATA_F, 0.0) # Zmienna sterująca / licznik pętli
    mem_glob.add("en", DataTypes.DATA_B, True) # Zmienna sterująca
    
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

    # --- BLOK 1: CLOCK ---
    # Generuje impulsy, dopóki Logic zwraca True.
    # EN = blk_logic.out[0] (Wynik porównania)
    # Period = 1000ms, Width = 100ms
    blk_clock = BlockClock(
        block_idx=1,
        storage=storage,
        enable=blk_logic.out[1], 
        period_ms=10000.0,
        width_ms=100.0
    )


    blk_for = BlockFor(
        block_idx=2,
        storage=storage,
        condition = ForCondition.LT,
        start=0,
        limit=100,
        step=1,
        chain_len=2,
        en=blk_clock.out[0]
    )

    blk_counter = BlockCounter(
        block_idx=3,
        storage=storage,
        cu=blk_for.out[0],
        reset=False,        # Stała False
        step=1.0,           # Krok
        start_val=0.0,      # Start od 0
        limit_max=1000,
        cfg_mode=CounterConfig.CFG_ON_RISING # Zliczanie impulsów (Counter)
    )

    # --- BLOK 3: SET ---
    # Przepisuje wartość licznika do zmiennej globalnej "x".
    # To zamyka pętlę sprzężenia zwrotnego: x rośnie -> Logic sprawdza x -> Clock działa -> Counter rośnie -> Set aktualizuje x.
    blk_set = BlockSet(
        block_idx=4,
        storage=storage,
        target=Ref("x"),
        value=blk_counter.out[1] # CV (Current Value)
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