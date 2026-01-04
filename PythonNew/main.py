import sys
from EmulatorMemory import EmulatorMemory, DataTypes
from EmulatorMemoryReferences import Global
from BlocksStorage import BlocksStorage
from BlockBase import BlockBase
from BlockFor import BlockFor, ForCondition, ForOperator
from BlockMath import BlockMath
from BlockLogic import BlockLogic
from FullDump import FullDump
from BlockTimer import BlockTimer, TimerType


if __name__ == "__main__":
    print("=== ROZPOCZĘCIE GENEROWANIA ZŁOŻONEGO TESTU ===\n")

    # -------------------------------------------------------------------------
    # 1. SETUP PAMIĘCI
    # -------------------------------------------------------------------------
    Global.clear_memories()
    
    # Konteksty pamięci
    mem_glob = EmulatorMemory(context_id=0) # Globalne
    mem_blk  = EmulatorMemory(context_id=1) # Wyjścia bloków
    
    # Rejestracja dla buildera referencji
    Global.register_memory(mem_glob)
    Global.register_memory(mem_blk)

    # Definicja zmiennych globalnych (Parametry symulacji)
    mem_glob.add("Multiplier", DataTypes.DATA_F, 2.5)   # Mnożnik
    mem_glob.add("BaseOffset", DataTypes.DATA_F, 10.0)  # Stały dodatek
    mem_glob.add("AlarmThreshold", DataTypes.DATA_UI8, 200) # Próg alarmowy
    mem_glob.add("settings",  DataTypes.DATA_UI32, [10,20,999999], [3])
    mem_glob.add("limit", DataTypes.DATA_UI32, 50) #loop
    
    # Przeliczenie indeksów (niezbędne przed użyciem w blokach)
    mem_glob.recalculate_indices()

    # Inicjalizacja Storage Bloków
    storage = BlocksStorage(mem_blk)

    # -------------------------------------------------------------------------
    # 2. KONFIGURACJA BLOKÓW
    # -------------------------------------------------------------------------
    print("--- Tworzenie struktury bloków ---\n")

    blk_timer = BlockTimer(
        block_idx=0,
        storage=storage,
        timer_type=TimerType.TON,
        pt =3000
    )

    blk_loop = BlockFor(
        block_idx=1,
        storage=storage,
        chain_len=2,
        en=blk_timer.out[0],
        start=0.0,
        limit=Global("settings")[2],
        step=1.0,
        condition=ForCondition.LT,
        operator=ForOperator.ADD
    )

    # --- BLOK 101: MATH (Wewnątrz pętli - Krok 1) ---
    # Działanie: Wynik = Iterator * Multiplier
    # Inputs:
    #   1. blk_loop.out[1] -> Iterator (Double)
    #   2. Global("Multiplier") -> Zmienna Globalna
    blk_scale = BlockMath(
        block_idx=2,
        storage=storage,
        expression="in_1 * in_2",
        en=blk_loop.out[0],
        connections=[
            blk_loop.out[1],      # in_1: Iterator pętli
            Global("Multiplier")  # in_2: Globalny parametr
        ]
    )

    blk_offset = BlockMath(
        block_idx=3,
        storage=storage,
        en=blk_scale.out[0],
        expression="in_1 + in_2",
        connections=[
            blk_scale.out[1],     # in_1: Wynik mnożenia
            Global("BaseOffset")  # in_2: Offset
        ]
    )

    # --- BLOK 200: LOGIC (Poza pętlą) ---
    # Działanie: Sprawdza czy ostatni wynik > AlarmThreshold
    # Inputs:
    #   1. blk_offset.out[1] -> Wynik ostatniej operacji w pętli
    #   2. Global("AlarmThreshold")
    blk_alarm = BlockLogic(
        block_idx=4,
        storage=storage,
        expression="in_1 > in_2",
        connections=[
            blk_offset.out[1],       # in_1: Końcowy wynik obliczeń
            Global("AlarmThreshold") # in_2: Próg
        ]
    )

    # -------------------------------------------------------------------------
    # 3. FINALIZACJA
    # -------------------------------------------------------------------------
    
    # Przeliczenie indeksów pamięci bloków (bo doszły zmienne wyjściowe z każdego bloku)
    mem_blk.recalculate_indices()
    
    # Przeliczenie masek użycia wejść (dla pewności)
    storage.recalculate_connection_masks()

    # -------------------------------------------------------------------------
    # 4. GENEROWANIE DUMP
    # -------------------------------------------------------------------------
    
    # Obiekt generujący pełny zrzut
    dump = FullDump(mem_glob, mem_blk, storage)
    
    # A. Wypisanie na ekran
    print("=== PODGLĄD DANYCH WYJŚCIOWYCH ===")
    dump.write(sys.stdout)
    
    # B. Zapis do pliku
    output_filename = "test.txt"
    with open(output_filename, "w") as f:
        dump.write(f)
        
    print(f"\n[SUKCES] Wygenerowano plik: {output_filename}")