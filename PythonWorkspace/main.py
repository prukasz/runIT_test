from Enums import DataTypes
from BlockStorage import BlocksStorage
from GlobalStorage import GlobalStorage
from GlobalReferences import Global as G
from FullDump import FullDump

# Importy bloków
from BlockLogic import BlockLogic
from BlockSetGlobal import BlockSetGlobal
from BlockTimer import BlockTimer, TimerType
from BlockMath import BlockMath

def main():
    # 1. Inicjalizacja
    glob_store = GlobalStorage()
    blk_store = BlocksStorage()
    G.set_store(glob_store)

    # Zmienna na wynik (żebyśmy widzieli efekt działania)
    glob_store.add_scalar("RESULT_VAL", DataTypes.DATA_D, 0.0)

    print("--- 1. Tworzenie Bloków ---")

    # ---------------------------------------------------------
    # BLOK 0: LOGIC (Zawsze Prawda)
    # Warunek startowy. Używamy prostego "1 == 1".
    # ---------------------------------------------------------
    b_start = BlockLogic(
        block_idx=0,
        expression="1 == 1", 
        in_list=[] 
    )
    blk_store.add_block(b_start)

    # ---------------------------------------------------------
    # BLOK 1: TIMER (TON - 3 sekundy)
    # ---------------------------------------------------------
    b_timer = BlockTimer(
        block_idx=1,
        timer_type=TimerType.TOF,
        pt=3000  # 3000 ms = 3s
    )
    blk_store.add_block(b_timer)

    # ---------------------------------------------------------
    # BLOK 2: MATH (10 * 2137)
    # Wykona się, gdy Timer poda sygnał na wejście EN
    # ---------------------------------------------------------
    b_math = BlockMath(
        block_idx=2,
        expression="10 * 2137"
    )
    blk_store.add_block(b_math)

    # ---------------------------------------------------------
    # BLOK 3: SET GLOBAL (Zapis wyniku)
    # ---------------------------------------------------------
    b_set = BlockSetGlobal(
        block_idx=3,
        target_ref=G("RESULT_VAL").build()
    )
    blk_store.add_block(b_set)

    print("--- 2. Łączenie (Wiring) ---")

    # 1. Logic -> Timer
    # Logic Out 1 (Result) ---> Timer In 0 (EN)
    # Gdy Logic da TRUE, Timer zaczyna liczyć.
    b_start.connect(1, b_timer, 0)

    # 2. Timer -> Math
    # Timer Out 0 (Q) ---> Math In 0 (EN)
    # Math wykona się (będzie Enabled) dopiero gdy Timer skończy liczyć (Q=True).
    b_timer.connect(0, b_math, 0)

    # 3. Math -> Set Global
    # Math Out 1 (Result) ---> SetGlobal In 0 (Value)
    b_math.connect(1, b_set, 0)

    print("--- 3. Generowanie Pliku ---")
    filename = "test.txt"
    dumper = FullDump(glob_store, blk_store)
    
    with open(filename, "w") as f:
        dumper.write(f)
    
    print(f"Gotowe! Plik '{filename}' wygenerowany.")
    
    # Podgląd fragmentu z Math (dla pewności, że stałe weszły)
    with open(filename, "r") as f:
        print("\n--- Sprawdzenie stałych w MATH ---")
        for line in f:
            if "LOGIC CONST" in line: # Math używa tego samego parsera stałych co Logic
                print(line.strip())

if __name__ == "__main__":
    main()