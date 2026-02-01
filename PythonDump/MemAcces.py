import struct
import ctypes as ct
from dataclasses import dataclass, field
from typing import List, Union, Any, Dict, Optional, Tuple
from Enums import mem_types_t
from Mem import mem_context_t, mem_instance_t

# ============================================================================
# 1. C-COMPATIBLE STRUCTURES
# ============================================================================

class access_packet_t(ct.LittleEndianStructure):
    """
    Maps to C struct:
    typedef struct __packed{
        uint8_t  type       :4;
        uint8_t  ctx_id     :3;
        uint8_t  is_index_resolved:1;
        uint8_t  dims_cnt   :3;
        uint8_t  idx_type   :3;
        uint8_t  reserved   :2; 
        uint16_t instance_idx;
    }access_packet;
    
    Total: 4 bytes
    """
    _pack_ = 1
    _fields_ = [
        # Byte 0
        ("type",              ct.c_uint8, 4),
        ("ctx_id",            ct.c_uint8, 3),
        ("is_index_resolved", ct.c_uint8, 1),
        # Byte 1
        ("dims_cnt",          ct.c_uint8, 3),
        ("idx_type",          ct.c_uint8, 3),
        ("reserved",          ct.c_uint8, 2),
        # Bytes 2-3
        ("instance_idx",      ct.c_uint16),
    ]

# ============================================================================
# 2. ACCESS MANAGER - Registry for all contexts
# ============================================================================

class AccessManager:
    """
    Manages all memory contexts and provides alias resolution.
    Aliases are assumed unique across all contexts.
    Resolves aliases dynamically from registered contexts.
    """
    _instance: Optional['AccessManager'] = None
    
    def __init__(self):
        self.contexts: List[mem_context_t] = []
    
    @classmethod
    def get_instance(cls) -> 'AccessManager':
        if cls._instance is None:
            cls._instance = AccessManager()
        return cls._instance
    
    @classmethod
    def reset(cls):
        cls._instance = None
    
    def register_context(self, ctx: mem_context_t):
        """Register a context for alias resolution."""
        # Avoid duplicate registration
        for existing in self.contexts:
            if existing.ctx_id == ctx.ctx_id:
                return  # Already registered
        self.contexts.append(ctx)
    
    def resolve_alias(self, alias: str) -> Tuple[int, mem_types_t, int, mem_instance_t]:
        """
        Resolve alias to (ctx_id, type, instance_idx, instance).
        Searches all registered contexts dynamically.
        """
        for ctx in self.contexts:
            if alias in ctx.alias_map:
                m_type, idx = ctx.alias_map[alias]
                instance = ctx.storage[m_type][idx]
                return (ctx.ctx_id, m_type, idx, instance)
        
        raise KeyError(f"Alias '{alias}' not found in any registered context")

# ============================================================================
# 3. REF CLASS - Fluent API for building references
# ============================================================================

class Ref:
    """
    Builds memory access references.
    
    Usage:
        Ref("temperature")           # Scalar access
        Ref("matrix")[10, 5]         # Static array access  
        Ref("matrix")[10][Ref("i")]  # Mixed: static + dynamic index
        Ref("matrix")[Ref("x"), Ref("y")]  # Fully dynamic
    """
    
    def __init__(self, alias: str):
        self.alias = alias
        self.indices: List[Tuple[bool, Union[int, 'Ref']]] = []  # (is_static, value)
    
    def __getitem__(self, item) -> 'Ref':
        """Handle [] operator for array indexing."""
        items = item if isinstance(item, tuple) else (item,)
        
        for it in items:
            if isinstance(it, int):
                # Static index (e.g., [10])
                self.indices.append((True, it))
            elif isinstance(it, Ref):
                # Dynamic index via Ref object
                self.indices.append((False, it))
            elif isinstance(it, str):
                # Dynamic index via alias string -> convert to Ref
                self.indices.append((False, Ref(it)))
            else:
                raise TypeError(f"Invalid index type: {type(it)}. Use int or Ref.")
        return self
    
    def _can_resolve(self) -> bool:
        """Check if all indices are static (can be resolved at compile time)."""
        return all(is_static for is_static, _ in self.indices)
    
    def _compute_resolved_index(self, instance: mem_instance_t) -> int:
        """
        Compute flat index from multi-dimensional static indices.
        Uses row-major order (C-style).
        """
        if not self._can_resolve():
            return 0
        
        dims = [d.value if hasattr(d, 'value') else int(d) for d in instance.dims]
        
        if len(self.indices) != len(dims):
            raise ValueError(f"Index count mismatch: got {len(self.indices)}, expected {len(dims)}")
        
        flat_index = 0
        stride = 1
        
        # Row-major: rightmost dimension varies fastest
        for i in range(len(dims) - 1, -1, -1):
            _, idx_val = self.indices[i]
            if idx_val >= dims[i]:
                raise IndexError(f"Index {idx_val} out of bounds for dimension {i} (size {dims[i]})")
            flat_index += idx_val * stride
            stride *= dims[i]
        
        return flat_index
    
    def to_bytes(self, manager: Optional[AccessManager] = None) -> bytes:
        """
        Serialize this reference to bytes matching C access_packet format.
        """
        if manager is None:
            manager = AccessManager.get_instance()
        
        # 1. Resolve alias
        ctx_id, m_type, inst_idx, instance = manager.resolve_alias(self.alias)
        
        # 2. Build header
        header = access_packet_t()
        header.type = m_type.value & 0xF
        header.ctx_id = ctx_id & 0x7
        header.dims_cnt = len(self.indices) & 0x7
        header.instance_idx = inst_idx
        
        # 3. Determine if can resolve
        can_resolve = self._can_resolve()
        
        # Scalar (no dims) or all static indices -> resolved
        if len(self.indices) == 0:
            header.is_index_resolved = 1
        elif can_resolve:
            header.is_index_resolved = 1
        else:
            header.is_index_resolved = 0
        
        # 4. Build mask (bit=1 means static uint16, bit=0 means dynamic Ref)
        idx_type_mask = 0
        for i, (is_static, _) in enumerate(self.indices):
            if is_static:
                idx_type_mask |= (1 << i)
        header.idx_type = idx_type_mask & 0x7
        
        # 5. Convert header to bytes
        blob = bytearray(bytes(header))
        
        # 6. Serialize indices payload
        for is_static, val in self.indices:
            if is_static:
                # Static: uint16_t value
                blob.extend(struct.pack('<H', val))
            else:
                # Dynamic: recursive access_packet
                blob.extend(val.to_bytes(manager))
        
        return bytes(blob)
    
    def __repr__(self) -> str:
        idx_str = ""
        for is_static, val in self.indices:
            if is_static:
                idx_str += f"[{val}]"
            else:
                idx_str += f"[{val!r}]"
        return f"Ref('{self.alias}'){idx_str}"

# ============================================================================
# 4. PACKET GENERATION HELPERS
# ============================================================================

def generate_access_storage_packet(refs: List[Ref]) -> bytes:
    """
    Generate packet for emu_mem_parse_access_create.
    Format: <ref_count: u16><total_indices: u16>
    """
    ref_count = len(refs)
    total_indices = sum(len(r.indices) for r in refs)
    return struct.pack('<HH', ref_count, total_indices)

def generate_access_packets(refs: List[Ref], manager: Optional[AccessManager] = None) -> List[bytes]:
    """
    Generate all access packets for a list of references.
    """
    if manager is None:
        manager = AccessManager.get_instance()
    return [ref.to_bytes(manager) for ref in refs]

# ============================================================================
# 5. DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from Mem import mem_context_t
    from Enums import mem_types_t
    
    print("=" * 60)
    print("Memory Access Packet Generator Test")
    print("=" * 60)
    
    # 1. Create context with variables
    ctx = mem_context_t(ctx_id=1)
    ctx.add(mem_types_t.MEM_F,   alias="temperature", data=25.5)
    ctx.add(mem_types_t.MEM_F,   alias="humidity",    data=60.2)
    ctx.add(mem_types_t.MEM_U16, alias="matrix",      dims=[10, 5], data=[0]*50)
    ctx.add(mem_types_t.MEM_I16, alias="row_idx",     data=3)
    ctx.add(mem_types_t.MEM_I16, alias="col_idx",     data=2)
    
    # 2. Register context
    manager = AccessManager.get_instance()
    manager.register_context(ctx)
    
    print("\nRegistered aliases:", list(manager.global_alias_map.keys()))
    
    # 3. Test Cases
    print("\n--- Test A: Scalar Access ---")
    ref_a = Ref("temperature")
    pkt_a = ref_a.to_bytes()
    print(f"Ref: {ref_a}")
    print(f"Packet ({len(pkt_a)} bytes): {pkt_a.hex().upper()}")
    
    print("\n--- Test B: Static Array Access ---")
    ref_b = Ref("matrix")[3, 2]
    pkt_b = ref_b.to_bytes()
    print(f"Ref: {ref_b}")
    print(f"Packet ({len(pkt_b)} bytes): {pkt_b.hex().upper()}")
    
    print("\n--- Test C: Mixed Access (static + dynamic) ---")
    ref_c = Ref("matrix")[5][Ref("col_idx")]
    pkt_c = ref_c.to_bytes()
    print(f"Ref: {ref_c}")
    print(f"Packet ({len(pkt_c)} bytes): {pkt_c.hex().upper()}")
    
    print("\n--- Test D: Fully Dynamic Access ---")
    ref_d = Ref("matrix")[Ref("row_idx"), Ref("col_idx")]
    pkt_d = ref_d.to_bytes()
    print(f"Ref: {ref_d}")
    print(f"Packet ({len(pkt_d)} bytes): {pkt_d.hex().upper()}")
    
    print("\n--- Test E: Nested Dynamic (index from array) ---")
    # matrix[matrix[0,0], 1] - first index comes from another array element
    ref_e = Ref("matrix")[Ref("matrix")[0, 0], 1]
    pkt_e = ref_e.to_bytes()
    print(f"Ref: {ref_e}")
    print(f"Packet ({len(pkt_e)} bytes): {pkt_e.hex().upper()}")
    
    # 4. Storage allocation packet
    print("\n--- Storage Allocation Packet ---")
    all_refs = [ref_a, ref_b, ref_c, ref_d, ref_e]
    storage_pkt = generate_access_storage_packet(all_refs)
    print(f"Refs: {len(all_refs)}, Total indices: {sum(len(r.indices) for r in all_refs)}")
    print(f"Packet: {storage_pkt.hex().upper()}")
    
    print("\n" + "=" * 60)
    print("Packet Structure Reference:")
    print("  Byte 0: [type:4][ctx_id:3][is_resolved:1]")
    print("  Byte 1: [dims_cnt:3][idx_type:3][reserved:2]")
    print("  Bytes 2-3: instance_idx (uint16)")
    print("  Then for each dim: uint16 if static, or recursive packet if dynamic")
    print("=" * 60)
