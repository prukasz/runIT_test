import struct
from typing import Union, List
from enum import IntEnum
from BlockBase import BlockBase
from EmulatorMemory import EmulatorMemory
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from Enums import emu_types_t, block_type_t, emu_block_header_t
# =============================================================================
# 1. ENUMY DLA BLOKU FOR (ZGODNE Z KODEM C)
# =============================================================================

class ForCondition(IntEnum):
    GT  = 0x01 # >
    LT  = 0x02 # <
    GTE = 0x04 # >=
    LTE = 0x05 # <=

class ForOperator(IntEnum):
    ADD = 0x01
    SUB = 0x02
    MUL = 0x03
    DIV = 0x04

# =============================================================================
# 2. GŁÓWNA KLASA BLOKU FOR
# =============================================================================

class BlockFor(BlockBase):
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 chain_len: int,
                 start: Union[float, int, Ref, None] = 0.0,
                 limit: Union[float, int, Ref, None] = 0.0,
                 step: Union[float, int, Ref, None] = 1.0,
                 condition: ForCondition = ForCondition.LT,
                 operator: ForOperator = ForOperator.ADD,
                 en: Union[Ref, None] = None):
        
        self.config_start = 0.0
        self.config_limit = 0.0
        self.config_step  = 0.0
        
        processed_inputs = [None] * 4 
        processed_inputs[0] = en

        # Helper do logiki input/config
        def process_arg(arg, default_val):
            cfg_val = default_val
            inp_ref = None
            
            if isinstance(arg, (int, float)):
                cfg_val = float(arg)
                inp_ref = None # Wejście nieużywane, wartość z configu
            elif isinstance(arg, Ref):
                cfg_val = 0.0  # Placeholder w configu
                inp_ref = arg  # Wejście używane (referencja)
            
            return cfg_val, inp_ref

        self.config_start, processed_inputs[1] = process_arg(start, 0.0)
        self.config_limit, processed_inputs[2] = process_arg(limit, 0.0)
        self.config_step,  processed_inputs[3] = process_arg(step, 1.0)

        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_FOR,
            storage=storage,
            inputs=processed_inputs,
            output_defs=[
                (emu_types_t.DATA_B, []), # Output 0: ENO
                (emu_types_t.DATA_D, [])  # Output 1: Iterator
            ]
        )
        
        self.chain_len = chain_len
        self.condition = condition
        self.operator = operator

    def get_custom_data_packet(self) -> List[bytes]:
        packets = []
        # Packet 1: Doubles (01)
        h1 = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CONST.value)
        p1 = struct.pack('<ddd', self.config_start, self.config_limit, self.config_step)
        packets.append(h1 + p1)

        # Packet 2: Config (02)
        h2 = self._pack_common_header(emu_block_header_t.EMU_H_BLOCK_PACKET_CUSTOM.value)
        p2 = struct.pack('<HBB', self.chain_len, int(self.condition), int(self.operator))
        packets.append(h2 + p2)
        return packets

    def __str__(self):
        # ... (Formatowanie __str__ z BlockFor.py z poprzedniej odpowiedzi) ...
        base_output = self.get_hex_with_comments()
        lines = [base_output]
        custom_pkts = self.get_custom_data_packet()
        if len(custom_pkts) > 0:
            lines.append(f"#ID:{self.block_idx} FOR Limits# {custom_pkts[0].hex().upper()}")
        if len(custom_pkts) > 1:
            lines.append(f"#ID:{self.block_idx} FOR Settings# {custom_pkts[1].hex().upper()}")
        return "\n".join(lines)
# ==========================================
# TEST SAMODZIELNY
# ==========================================
if __name__ == "__main__":
    from EmulatorMemory import EmulatorMemory
    
    # 1. Przygotowanie środowiska (tak jak w main.py)
    Ref.clear_memories()
    mem_test = EmulatorMemory(1)
    
    # KLUCZOWE: Rejestracja pamięci, aby Global() widział zmienne 50_q0, 50_q1
    Ref.register_memory(mem_test)
    
    try:
        # 2. Tworzenie bloku (To dodaje zmienne do mem_test)
        blk = BlockFor(
            block_idx=50,
            chain_len=3,
            mem_blocks=mem_test,
            start=0,
            limit=10.5,
            step=1,
            condition=ForCondition.LTE,
            operator=ForOperator.ADD
        )
        
        # 3. Przeliczenie indeksów (Nadanie numerów ID zmiennym w pamięci)
        mem_test.recalculate_indices()

        print("--- TEST BLOCK FOR ---")
        print(blk)
        
    except Exception as e:
        print(f"Error: {e}")