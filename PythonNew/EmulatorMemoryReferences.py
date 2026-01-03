import struct
from dataclasses import dataclass, field
from typing import List, Optional, Union, Any

# ... (Klasa AccessNode bez zmian) ...
@dataclass
class AccessNode:
    is_array: bool
    type_id: int
    idx: int
    ref_id: int
    dims_cnt: int
    indices: List[Any] = field(default_factory=list)

    def to_bytes(self) -> bytes:
        # ... (Twoja implementacja z poprzedniego kroku, jest poprawna) ...
        payload = bytearray()
        header = ((self.type_id & 0x0F) << 4) | (self.dims_cnt & 0x0F)
        payload.append(header)
        payload.extend(struct.pack("<H", self.idx))
        
        if not self.is_array:
            ref_byte = self.ref_id & 0x0F
            payload.append(ref_byte)
        else:
            idx_types = 0
            for i, (is_dyn, _) in enumerate(self.indices):
                if is_dyn: idx_types |= (1 << i)
            config_byte = ((idx_types & 0x0F) << 4) | (self.ref_id & 0x0F)
            payload.append(config_byte)
            for is_dyn, val in self.indices:
                if is_dyn: payload.extend(val.to_bytes())
                else: payload.extend(struct.pack("<H", val))
        return bytes(payload)


class Global:
    """Builder class for creating references."""
    
    # ZMIANA: Lista pamięci zamiast jednej zmiennej
    _memories: List[Any] = [] 

    @classmethod
    def register_memory(cls, mem):
        """Dodaj kontekst pamięci do przeszukiwania."""
        cls._memories.append(mem)

    @classmethod
    def clear_memories(cls):
        cls._memories = []

    def __init__(self, alias: str, ref_id: int = 0):
        self.alias = alias
        self.ref_id = ref_id
        self.indices = []

    def __getitem__(self, items) -> 'Global':
        if not isinstance(items, tuple): items = (items,)
        for item in items:
            if isinstance(item, int): self.indices.append((False, item))
            elif isinstance(item, Global): self.indices.append((True, item))
            elif isinstance(item, str): self.indices.append((True, Global(item)))
            else: raise ValueError(f"Invalid index type: {type(item)}")
        return self

    def build(self) -> AccessNode:
        target_var = None
        found_in_ctx = -1

        # ZMIANA: Przeszukujemy wszystkie zarejestrowane pamięci
        for mem in Global._memories:
            if self.alias in mem.alias_map:
                target_var = mem.alias_map[self.alias]
                found_in_ctx = mem.context_id
                break
        
        if not target_var:
            # Fallback: Jeśli to dummy/nieznane
            print(f"[WARN] Unknown alias '{self.alias}'. Using dummy.")
            return AccessNode(False, 0, 0, 0, 0)

        # Opcjonalny Check: Czy ref_id w żądaniu zgadza się z kontekstem zmiennej?
        # Np. jeśli block.out[0] wymusza ref_id=1, a zmienna jest w ctx=1 -> OK.
        # Jeśli Global("Var") ma domyślne ref_id=0, a zmienna jest w ctx=0 -> OK.
        # W zaawansowanym systemie możemy tutaj nadpisać self.ref_id = found_in_ctx
        
        # WAŻNE: Aktualizujemy ref_id na podstawie faktycznego położenia zmiennej,
        # chyba że użytkownik wyraźnie wymusił co innego.
        # Tutaj zakładamy, że znaleziona zmienna determinuje ref_id.
        final_ref_id = self.ref_id 
        if final_ref_id != found_in_ctx:
             # print(f"[INFO] Auto-correcting RefID for {self.alias}: {self.ref_id} -> {found_in_ctx}")
             final_ref_id = found_in_ctx

        type_id = target_var.dtype.value
        var_idx = target_var.index
        dims_cnt = len(self.indices)
        
        processed_indices = []
        for is_dyn, val in self.indices:
            if is_dyn: processed_indices.append((True, val.build()))
            else: processed_indices.append((False, val))

        return AccessNode(
            is_array=(dims_cnt > 0),
            type_id=type_id,
            idx=var_idx,
            ref_id=final_ref_id, # Używamy skorygowanego lub wymuszonego ID
            dims_cnt=dims_cnt,
            indices=processed_indices
        )