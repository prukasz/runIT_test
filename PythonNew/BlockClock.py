import struct
from typing import Union, List
from BlockBase import BlockBase
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from Enums import block_type_t, emu_types_t, emu_block_header_t

class BlockClock(BlockBase):
    """
    Blok Zegara (Generator PWM).
    Działa ZAWSZE gdy EN=1 (czas płynie).
    
    Inputs:
    0: EN     (Bool) - Enable
    1: PERIOD (Double) - Okres [ms]
    2: WIDTH  (Double) - Czas trwania stanu wysokiego [ms]
    
    Outputs:
    0: Q (Bool)
    """

    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 enable: Union[Ref, bool] = True,
                 period_ms: Union[Ref, float] = 1000.0,
                 width_ms: Union[Ref, float] = 500.0):
        
        # Helper
        def process_arg(arg, default_val):
            if isinstance(arg, (int, float)):
                return float(arg), None
            return default_val, arg

        self.cfg_period, period_ref = process_arg(period_ms, 1000.0)
        self.cfg_width,  width_ref  = process_arg(width_ms, 500.0)
        
        # Enable: Jeśli True -> None (input niepodłączony w C = True domyślnie w logice)
        en_ref = enable if isinstance(enable, Ref) else None

        inputs = [en_ref, period_ref, width_ref]

        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_CLOCK, # Np. 0x0D - dodaj do Enums!
            storage=storage,
            inputs=inputs,
            output_defs=[
                (emu_types_t.DATA_B, []) # Q
            ]
        )

    def get_custom_data_packet(self) -> List[bytes]:
        # [CONST HEADER] [Period:8] [Width:8]
        header = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CONST.value)
        payload = struct.pack('<dd', self.cfg_period, self.cfg_width)
        return [header + payload]

    def __str__(self):
        # 1. Pobierz standardowe pakiety (Config, In, Out)
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        
        # 2. Pobierz pakiety Custom Data (Twoje stałe Period i Width)
        custom_pkts = self.get_custom_data_packet()
        
        # 3. Jeśli istnieją, dodaj je do wyniku
        if custom_pkts:
            # hex() zamienia bytes na string, upper() dla czytelności
            hex_str = custom_pkts[0].hex().upper()
            lines.append(f"#ID:{self.block_idx} CLOCK Const# {hex_str}")
            
        return "\n".join(lines)