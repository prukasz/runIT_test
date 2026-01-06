import struct
from typing import Union, List, Any
from enum import IntEnum
from BlockBase import BlockBase
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from Enums import block_type_t, emu_types_t, emu_block_header_t

# =============================================================================
# 1. ENUMY (Zgodne z block_counter.h)
# =============================================================================

class CounterConfig(IntEnum):
    CFG_ON_RISING   = 0  # Zliczanie na zboczu narastającym (Standardowy licznik)
    CFG_WHEN_ACTIVE = 1  # Zliczanie ciągłe (Akumulator)

# =============================================================================
# 2. GŁÓWNA KLASA BLOKU COUNTER
# =============================================================================

class BlockCounter(BlockBase):
    """
    Blok Licznika (Up/Down Counter).
    
    C Mapping:
    - Input 0: CU (Ref/Bool) - Trigger (Count Up)
    - Input 1: CD (Ref/Bool) - Trigger (Count Down)
    - Input 2: R  (Ref/Bool) - Reset
    - Input 3: STEP (Ref/Double) - Opcjonalny: Krok (nadpisuje config)
    - Input 4: MAX  (Ref/Double) - Opcjonalny: Max Limit (nadpisuje config)
    - Input 5: MIN  (Ref/Double) - Opcjonalny: Min Limit (nadpisuje config)
    
    - Output 0: ENO (Bool) - Zawsze True
    - Output 1: VAL (Double) - Aktualna wartość licznika
    """

    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 # Wejścia sterujące
                 cu: Union[Ref, None] = None,
                 cd: Union[Ref, None] = None,
                 reset: Union[Ref, None] = None,
                 # Parametry (mogą być wejściami dynamicznymi LUB konfiguracją stałą)
                 step: Union[float, int, Ref, None] = 1.0,
                 limit_max: Union[float, int, Ref, None] = 100.0,
                 limit_min: Union[float, int, Ref, None] = 0.0,
                 start_val: float = 0.0,
                 # Konfiguracja
                 cfg_mode: CounterConfig = CounterConfig.CFG_ON_RISING):
        
        # --- Przetwarzanie parametrów (Input vs Config) ---
        
        self.config_step = 1.0
        self.config_max = 100.0
        self.config_min = 0.0
        self.config_start = float(start_val)
        self.config_mode = cfg_mode

        processed_inputs = [None] * 6 # Miejsca na 6 wejść

        # --- Helper do obsługi sygnałów logicznych (CU, CD, RST) ---
        def process_signal(arg):
            # Jeśli Ref -> OK
            if isinstance(arg, Ref):
                return arg
            # Jeśli None lub False -> None (Brak połączenia w C = 0)
            if arg is None or arg is False:
                return None
            # Jeśli True -> Nieobsługiwane bezpośrednio bez stałych w pamięci
            # (można rzucić błąd lub wymagać Ref do stałej)
            if arg is True:
                raise ValueError(f"BlockCounter ID {block_idx}: Cannot use 'True' directly for signal. Use Ref to a constant.")
            return None

        processed_inputs[0] = process_signal(cu)
        processed_inputs[1] = process_signal(cd)
        processed_inputs[2] = process_signal(reset)

        # --- Helper do obsługi parametrów (Step, Max, Min) ---
        def process_param(arg, default_val):
            cfg_val = default_val
            inp_ref = None
            
            if isinstance(arg, (int, float)):
                cfg_val = float(arg)
                inp_ref = None  # Używamy wartości z configu
            elif isinstance(arg, Ref):
                cfg_val = default_val # Wartość domyślna w configu (zostanie nadpisana przez wejście)
                inp_ref = arg # Podłączamy wejście
            elif arg is None:
                cfg_val = default_val
                inp_ref = None
            
            return cfg_val, inp_ref

        # Parametry dynamiczne (3, 4, 5)
        self.config_step, processed_inputs[3] = process_param(step, 1.0)
        self.config_max,  processed_inputs[4] = process_param(limit_max, 100.0)
        self.config_min,  processed_inputs[5] = process_param(limit_min, 0.0)

        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_COUNTER, 
            storage=storage,
            inputs=processed_inputs,
            output_defs=[
                (emu_types_t.DATA_B, []), # Output 0: ENO
                (emu_types_t.DATA_D, [])  # Output 1: VAL
            ]
        )

    def get_custom_data_packet(self) -> List[bytes]:
        """
        Generuje pakiet konfiguracyjny (CUSTOM).
        Format C (block_counter.c / _emu_parse_counter_config):
        [CFG:1] [Start:8] [Step:8] [Max:8] [Min:8]
        Razem: 33 bajty
        """
        packets = []
        
        header = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CUSTOM.value)
        
        # struct.pack format: < B d d d d
        payload = struct.pack('<Bdddd', 
                              int(self.config_mode),
                              self.config_start,
                              self.config_step,
                              self.config_max,
                              self.config_min)
        
        packets.append(header + payload)
        return packets

    def __str__(self):
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        custom_pkts = self.get_custom_data_packet()
        if custom_pkts:
            lines.append(f"#ID:{self.block_idx} COUNTER Config# {custom_pkts[0].hex().upper()}")
        return "\n".join(lines)

# ==========================================
# TEST SAMODZIELNY
# ==========================================
if __name__ == "__main__":
    from EmulatorMemory import EmulatorMemory
    
    # 1. Setup
    Ref.clear_memories()
    mem_test = EmulatorMemory(1)
    Ref.register_memory(mem_test)
    
    storage = BlocksStorage(mem_test)

    try:
        # Zmienne sterujące (symulacja)
        mem_test.add("Signal_UP", emu_types_t.DATA_B, 0)
        mem_test.add("Signal_DOWN", emu_types_t.DATA_B, 0)
        
        # 2. Tworzenie bloku Counter
        # Przypadek: Reset niepodłączony (None/False), Step domyślny (1.0), Max ustawiony na stałe 500.0
        blk = BlockCounter(
            block_idx=60,
            storage=storage,
            cu=Ref("Signal_UP"),
            cd=Ref("Signal_DOWN"),
            reset=None,           # Brak resetu
            limit_max=500.0,      # Stała w configu
            limit_min=-50.0,
            start_val=10.0
        )
        
        # 3. Finalizacja
        mem_test.recalculate_indices()

        print("--- TEST BLOCK COUNTER ---")
        print(blk)
        
    except Exception as e:
        print(f"Error: {e}")