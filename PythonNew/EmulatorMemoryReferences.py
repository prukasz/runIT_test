import struct
import ctypes as ct
from dataclasses import dataclass, field
from typing import List, Union, Any, Dict
from enum import Enum
from Enums import emu_types_t

# ============================================================================
# 1. C-TYPES STRUCTURES (Odwzorowanie Firmware)
# ============================================================================

class NodeHeader(ct.LittleEndianStructure):
    """
    Odwzorowanie nagłówka 3-bajtowego:
    Byte 0: [Type (4 bits) | Dims (4 bits)] (Little Endian: Dims to LSB)
    Byte 1-2: Instance Index (uint16)
    """
    _pack_ = 1
    _fields_ = [
        ("dims_cnt",     ct.c_uint8, 4),  # bits 0-3
        ("target_type",  ct.c_uint8, 4),  # bits 4-7
        ("instance_idx", ct.c_uint16),    # bytes 1-2
    ]

class ScalarConfig(ct.LittleEndianStructure):
    """
    Konfiguracja dla Skalara (1 bajt):
    Byte 0: [Reserved (4) | Ref ID (4)]
    """
    _pack_ = 1
    _fields_ = [
        ("ref_id",   ct.c_uint8, 4), # bits 0-3
        ("reserved", ct.c_uint8, 4), # bits 4-7
    ]

class ArrayConfig(ct.LittleEndianStructure):
    """
    Konfiguracja dla Tablicy (1 bajt):
    Byte 0: [Idx Types Mask (4) | Ref ID (4)]
    """
    _pack_ = 1
    _fields_ = [
        ("ref_id",    ct.c_uint8, 4), # bits 0-3
        ("idx_types", ct.c_uint8, 4), # bits 4-7 (Maska: bit 0 = dim 0 dynamic)
    ]

# ============================================================================
# 2. INTERMEDIATE NODE REPRESENTATION
# ============================================================================

@dataclass
class AccessNode:
    is_array: bool
    type_id: int
    idx: int
    ref_id: int
    dims_cnt: int
    indices: List[Any] = field(default_factory=list)

    def to_bytes(self) -> bytes:
        # 1. Tworzenie Nagłówka
        header = NodeHeader()
        header.dims_cnt = self.dims_cnt & 0xF
        header.target_type = self.type_id & 0xF
        header.instance_idx = self.idx
        
        # Konwersja struktury C na bytearray
        blob = bytearray(bytes(header))

        # 2. Skalar
        if not self.is_array:
            cfg = ScalarConfig()
            cfg.ref_id = self.ref_id & 0xF
            blob.extend(bytes(cfg))
        
        # 3. Tablica
        else:
            # Obliczanie maski typów indeksów (bit 1 = dynamiczny, 0 = statyczny)
            mask = 0
            for i, (is_dyn, _) in enumerate(self.indices):
                if is_dyn: mask |= (1 << i)
            
            cfg = ArrayConfig()
            cfg.ref_id = self.ref_id & 0xF
            cfg.idx_types = mask & 0xF
            blob.extend(bytes(cfg))

            # Serializacja indeksów (Payload)
            for is_dyn, val in self.indices:
                if is_dyn:
                    # REKURENCJA: Jeśli indeks dynamiczny, to 'val' jest obiektem AccessNode
                    blob.extend(val.to_bytes()) 
                else:
                    # STATIC: Jeśli statyczny, to 'val' jest intem (uint16)
                    blob.extend(struct.pack("<H", val)) 
                    
        return bytes(blob)

# ============================================================================
# 3. MOCK MEMORY SYSTEM (Symulacja Kontekstów)
# ============================================================================



@dataclass
class MockVar:
    dtype: emu_types_t
    index: int

class MockContext:
    def __init__(self, ctx_id: int):
        self.context_id = ctx_id
        self.alias_map: Dict[str, MockVar] = {}

    def add(self, name: str, dtype: emu_types_t, idx: int):
        self.alias_map[name] = MockVar(dtype, idx)

# ============================================================================
# 4. BUILDER CLASS (Fluent API)
# ============================================================================

class Ref:
    """Klasa budująca referencje, np: Ref('array')[10]['index']"""
    
    # Rejestr wszystkich dostępnych kontekstów pamięci
    _memories: List[MockContext] = [] 

    @classmethod
    def register_context(cls, mem: MockContext):
        cls._memories.append(mem)
    
    @classmethod
    def clear_contexts(cls):
        cls._memories = []

    def __init__(self, alias: str, explicit_ref_id: int = 0):
        self.alias = alias
        self.ref_id = explicit_ref_id
        self.indices = []

    def __getitem__(self, item) -> 'Ref':
        """Obsługa operatora [] dla indeksowania tablic"""
        # Obsługa wielu wymiarów [x, y]
        items = item if isinstance(item, tuple) else (item,)
        
        for it in items:
            if isinstance(it, int):
                # Indeks Statyczny (np. [10])
                self.indices.append((False, it))
            elif isinstance(it, str):
                # Indeks Dynamiczny przez nazwę (np. ['var_name'])
                self.indices.append((True, Ref(it)))
            elif isinstance(it, Ref):
                # Indeks Dynamiczny przez obiekt Ref
                self.indices.append((True, it))
            else:
                raise ValueError(f"Invalid index type: {type(it)}")
        return self

    def build(self) -> AccessNode:
        """Rozwiązuje nazwy na ID i zwraca gotowy węzeł"""
        target_var = None
        found_in_ctx = -1

        # 1. Szukanie zmiennej w zarejestrowanych kontekstach
        for mem in Ref._memories:
            if self.alias in mem.alias_map:
                target_var = mem.alias_map[self.alias]
                found_in_ctx = mem.context_id
                break
        
        # Fallback dla nieznanych
        if not target_var:
            print(f"[WARN] Unknown alias '{self.alias}'. Using dummy vals.")
            # Zwracamy dummy node, żeby kod się nie wywalił
            return AccessNode(False, 0, 0, 0, 0)

        # 2. Ustalanie Context ID
        # Jeśli użytkownik podał ID ręcznie w __init__, użyj go. Jeśli nie (0), użyj znalezionego.
        final_ref_id = self.ref_id if self.ref_id != 0 else found_in_ctx
        
        # 3. Rekurencyjne budowanie indeksów dynamicznych
        processed_indices = []
        for is_dyn, val in self.indices:
            if is_dyn:
                processed_indices.append((True, val.build()))
            else:
                processed_indices.append((False, val))

        # 4. Zwrócenie węzła
        return AccessNode(
            is_array=(len(self.indices) > 0),
            type_id=target_var.dtype.value,
            idx=target_var.index,
            ref_id=final_ref_id,
            dims_cnt=len(self.indices),
            indices=processed_indices
        )

# ============================================================================
# 5. DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    print("--- Konfiguracja Pamięci ---")
    
    # 1. Tworzymy kontekst pamięci (np. ID = 1)
    ctx1 = MockContext(ctx_id=1)
    
    # 2. Dodajemy zmienne do kontekstu
    # Tablica floatów 2D pod indeksem 100
    ctx1.add("matrix_2d", emu_types_t.DATA_F, 100) 
    # Zmienna int16 pod indeksem 5 (będzie użyta jako indeks dynamiczny)
    ctx1.add("dynamic_idx", emu_types_t.DATA_I16, 5)
    # Zwykły skalar
    ctx1.add("temperature", emu_types_t.DATA_F, 20)
    # 3. Rejestrujemy kontekst w builderze
    Ref.register_context(ctx1)
    print("--- Generowanie Pakietów ---")

    # TEST A: Zwykły skalar
    # temperature (idx 20, type float=6)
    node_a = Ref("temperature").build()
    print(f"\n[A] Scalar 'temperature': {node_a.to_bytes().hex().upper()}")

    node_b = Ref("matrix_2d")[10, 2].build()
    print(f"\n[B] Static Array 'matrix_2d[10][2]': {node_b.to_bytes().hex().upper()}")

    node_c = Ref("matrix_2d")[10]["dynamic_idx"].build()
    print(f"\n[C] Mixed Array 'matrix_2d[10][dynamic_idx]': {node_c.to_bytes().hex().upper()}")
