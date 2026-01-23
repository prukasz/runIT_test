import ctypes as ct
from enum import IntEnum

# 1. Wspólny Enum dla wszystkich typów
class EmuDataType(IntEnum):
    DATA_UI8  = 0
    DATA_UI16 = 1
    DATA_UI32 = 2
    DATA_I8   = 3
    DATA_I16  = 4
    DATA_I32  = 5
    DATA_F    = 6
    DATA_D    = 7
    DATA_B    = 8

# 2. Inteligentna funkcja fabrykująca
def emu_mem_instance(context_id, target_type, dim_sizes_list=None):
    """
    Tworzy strukturę binarną C (Little Endian, packed).
    
    Automatyzacja:
    - updated = 1
    - _reserved = 0
    - dims_cnt = len(dim_sizes_list)
    - Jeśli dim_sizes_list jest puste/None -> tworzy strukturę 2-bajtową (Scalar).
    - Jeśli dim_sizes_list ma elementy -> tworzy strukturę z flexible array member.
    """
    
    # Zabezpieczenie na wypadek braku listy (skalar)
    if dim_sizes_list is None:
        dim_sizes_list = []
        
    array_len = len(dim_sizes_list)

    # Definiujemy klasę dynamicznie wewnątrz funkcji
    class emu_mem_instance_dynamic(ct.LittleEndianStructure):
        _pack_ = 1
        
        # Bazowe pola bitowe (zawsze obecne)
        _common_fields = [
            ("context_id",    ct.c_uint16, 3),
            ("dims_cnt",      ct.c_uint16, 3),
            ("target_type",   ct.c_uint16, 4),
            ("updated",       ct.c_uint16, 1),
            ("_reserved",     ct.c_uint16, 5),
        ]
        
        # Decyzja: Czy dodajemy tablicę?
        if array_len > 0:
            _fields_ = _common_fields + [("dim_sizes", ct.c_uint8 * array_len)]
        else:
            _fields_ = _common_fields

        def __repr__(self):
            # Ładna nazwa typu
            try:
                t_name = EmuDataType(self.target_type).name
            except ValueError:
                t_name = str(self.target_type)

            # Czy to tablica czy skalar?
            if hasattr(self, 'dim_sizes'):
                sizes = list(self.dim_sizes)
                return (f"<Instance ARR ctx={self.context_id} type={t_name} "
                        f"dims_cnt={self.dims_cnt} sizes={sizes}>")
            else:
                return (f"<Instance SCALAR ctx={self.context_id} type={t_name} "
                        f"dims_cnt={self.dims_cnt}>")

    # --- TWORZENIE OBIEKTU ---
    instance = emu_mem_instance_dynamic()
    
    # Wypełnianie automatem
    instance.context_id = context_id
    instance.dims_cnt = array_len      # Automat
    instance.updated = 1               # Automat (zawsze 1)
    instance._reserved = 0             # Automat (zawsze 0)
    
    # Obsługa typu (Enum lub int)
    if isinstance(target_type, EmuDataType):
        instance.target_type = target_type.value
    else:
        instance.target_type = int(target_type)

    # Wypełnianie tablicy (tylko jeśli istnieje)
    if array_len > 0:
        for i, size in enumerate(dim_sizes_list):
            instance.dim_sizes[i] = size
            
    return instance

# ==========================================
# PRZYKŁADY
# ==========================================
if __name__ == "__main__":
    
    print("--- 1. Przypadek Array (jak chciałeś) ---")
    # emu_mem_instance(context, type, sizes)
    # Reszta dzieje się sama.
    
    arr_inst = emu_mem_instance(1, EmuDataType.DATA_D, [10, 10, 10])
    
    print(f"Obiekt: {arr_inst}")
    print(f"Bytes:  {bytes(arr_inst).hex().upper()}")
    # Oczekiwane:
    # ctx=1 (001), dims=3 (011), type=7 (0111), upd=1 (1)
    # Bin: ...1 0111 011 001 -> 0x05D9 -> D9 05
    # Sizes: 0A 0A 0A
    # Wynik: D9 05 0A 0A 0A

    print("\n--- 2. Przypadek Scalar (len=0) ---")
    
    scalar_inst = emu_mem_instance(2, EmuDataType.DATA_F, [])
    # Lub: emu_mem_instance(2, EmuDataType.DATA_F)
    
    print(f"Obiekt: {scalar_inst}")
    print(f"Bytes:  {bytes(scalar_inst).hex().upper()}")
    print(f"Size:   {ct.sizeof(scalar_inst)} bytes")
    # Oczekiwane:
    # ctx=2 (010), dims=0 (000), type=6 (0110), upd=1 (1)
    # Bin: ...1 0110 000 010 -> 0x05C2 -> C2 05
    # Wynik: C2 05 (Tylko 2 bajty!)