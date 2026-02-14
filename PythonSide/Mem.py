import ctypes as ct
from enum import IntEnum
from dataclasses import dataclass, field
from typing import Any, List, Dict, Optional, Tuple, Union
import struct

from Enums import mem_types_t, mem_types_map, mem_types_pack_map, mem_types_size, packet_header_t

class instance_head_t(ct.LittleEndianStructure):
    """Bitfield structure for memory instance header, for packet creation"""
    _pack_ = 1
    #New version new fields, in padding, reverse compatible
    _fields_ = [
        ("context",           ct.c_uint16, 3),
        ("dims_cnt",          ct.c_uint16, 4),
        ("mem_type",          ct.c_uint16, 4),
        ("updated",           ct.c_uint16, 1), #has been updated in cycle
        ("is_clearable",      ct.c_uint16, 1), #can clear updated
        ("is_standalone",     ct.c_uint16, 1), #Pointer at non ctx data
        ("is_mutable",        ct.c_uint16, 1), #can value be edited
        ("reserved",          ct.c_uint16, 1), 
    ]

@dataclass 
class mem_instance_t:
    """Represents an instance in context, stores data in python only"""
    alias: str    #python side alias only
    my_index: int #index in context separately for each type and context

    head: instance_head_t

    dims: List[ct.c_uint16] #list of dimensions if array, if scalar empty
    data: Any #shall match mem_type, list if array, scalar if scalar

    def __init__(self, ctx: int, mem_type: mem_types_t, alias: str, data: Any, dims: Optional[List[int]], can_clear: int, is_standalone: int, is_mutable: int):
        self.alias = alias
        self.head = instance_head_t()
        self.head.context = ctx
        self.head.is_clearable = can_clear
        self.head.is_standalone = is_standalone
        self.head.is_mutable = is_mutable
        self.head.mem_type = mem_type.value
        self.dims = [ct.c_uint16(d) for d in dims] if dims else []
        self.data = data
        self.my_index = -1 #to be set when added to context

        self.head.dims_cnt = len(dims if dims is not None else [])

    def pack_instance(self) -> bytes:
        header_bytes = bytes(self.head) 
        dims_bytes = b''
        if self.head.dims_cnt > 0:
            ArrayType = ct.c_uint16 * self.head.dims_cnt
            raw_values = [int(d) for d in self.dims]
            c_array = ArrayType(*raw_values)
            dims_bytes = bytes(c_array)
        return header_bytes + dims_bytes
    
    def pack_data(self) -> bytes:
        """Packs Values based on type"""
        if self.data is None: return b''
        
        try:
            m_type = mem_types_t(self.head.type)
            c_type = mem_types_map.get(m_type)
        except ValueError:
            c_type = None
        if not c_type: raise ValueError(f"Unknown Type: {self.head.type}")

        values = self.data if isinstance(self.data, list) else [self.data]
        ArrayType = c_type * len(values)
        return bytes(ArrayType(*values))

@dataclass
class mem_context_t:
    """same fucntionality as C side with extra info"""
    ctx_id: int
    pkt_size: int = 512
    storage: Dict[mem_types_t, List[mem_instance_t]] = field(default_factory=dict)
    # alias map for access to instances by alias: stores (mem_type, instance)
    alias_map: Dict[str, Tuple[mem_types_t, mem_instance_t]] = field(default_factory=dict)

    def __init__(self, ctx_id: int, pkt_size: int = 512):
        self.ctx_id = ctx_id
        self.pkt_size = pkt_size
        self.storage = {t: [] for t in mem_types_t}
        self.alias_map = {}

    def add(self, type: mem_types_t, alias: str, data: Any = None, dims: Optional[List[int]] = None, can_clear: int = 1, is_standalone: int = 0, is_mutable: int = 1) -> mem_instance_t:
        """
        Adds an instance to the context.
        :param alias: Unique string name to refer to this variable later.
        """
        #Calculate the next index for this type
        current_list = self.storage[type]
        next_idx = len(current_list)

        if alias in self.alias_map:
            raise ValueError(f"Alias '{alias}' already exists in Context {self.ctx_id}")


        #Create Instance
        new_inst = mem_instance_t(
            ctx=self.ctx_id,
            mem_type=type,
            dims=dims,
            data=data,
            can_clear=can_clear,
            is_standalone=is_standalone,
            is_mutable=is_mutable,
            alias=alias,
        )
        new_inst.my_index = next_idx
        self.alias_map[alias] = (type, new_inst)
        
        current_list.append(new_inst)
        return new_inst
    
    def get_by_alias(self, alias: str) -> mem_instance_t:
        """Get instance by alias, raises if not found"""
        if alias not in self.alias_map:
            raise ValueError(f"Alias '{alias}' not found in Context {self.ctx_id}")
        return self.alias_map[alias][1]

    def remove_by_alias(self, alias: str) -> None:
        """Remove an instance by alias and recalculate indices for that type."""
        if alias not in self.alias_map:
            raise ValueError(f"Alias '{alias}' not found in Context {self.ctx_id}")

        m_type, inst = self.alias_map.pop(alias)
        self.storage[m_type].remove(inst)

        # recalculate my_index for remaining instances of the same type
        for new_idx, remaining in enumerate(self.storage[m_type]):
            remaining.my_index = new_idx

    #refactor required?
    def packet_generate_cfg(self) -> bytes:
        packet = struct.pack('<B', self.ctx_id)
        for m_type in mem_types_t:
            instances = self.storage.get(m_type, [])

            heap_elements = 0
            instance_count = len(instances)
            total_dims_rank = 0

            for inst in instances:
                dims_cnt = len(inst.dims)
                total_dims_rank += dims_cnt
                if dims_cnt == 0:
                    heap_elements += 1
                else:
                    product = 1
                    for d in inst.dims:
                        val = d.value if hasattr(d, 'value') else int(d)
                        product *= val
                    heap_elements += product

            packet += struct.pack('<IHH', heap_elements, instance_count, total_dims_rank)
        return packet 
    


    #refactor required? 
    def generate_packets_instances(self) -> List[bytes]:
        packets = []
        for m_type in sorted(self.storage.keys()):
            current_buffer = bytearray()
            for inst in self.storage[m_type]:
                instance_bytes = inst.pack_instance()
                inst_len = len(instance_bytes)
                if inst_len > self.pkt_size: raise ValueError("Instance too big")
                
                if len(current_buffer) + inst_len > self.pkt_size:
                    packets.append(bytes(current_buffer))
                    current_buffer = bytearray()
                current_buffer.extend(instance_bytes)
            
            if len(current_buffer) > 0: packets.append(bytes(current_buffer))
        return packets
    
    
    #refactor required? 
    def generate_packets_scalar_data(self) -> List[bytes]:
        """Generate packets for scalar variable data only."""
        packets = []
        for m_type in sorted(self.storage.keys()):
            if not self.storage[m_type]: continue

            pkt_header = struct.pack('<BBB', self.ctx_id, m_type.value, 0)
            current_buffer = bytearray(pkt_header)
            count_in_pkt = 0
            for idx, inst in enumerate(self.storage[m_type]):
                if inst.head.dims_cnt > 0 or inst.data is None: continue
                data_bytes = inst.pack_data()
                entry_header = struct.pack('<H', idx)
                if len(current_buffer) + 2 + len(data_bytes) > self.pkt_size:
                    current_buffer[2] = count_in_pkt
                    packets.append(bytes(current_buffer))
                    current_buffer = bytearray(pkt_header)
                    count_in_pkt = 0
                current_buffer.extend(entry_header)
                current_buffer.extend(data_bytes)
                count_in_pkt += 1
            if count_in_pkt > 0:
                current_buffer[2] = count_in_pkt
                packets.append(bytes(current_buffer))
        return packets
    
    #refactor required?
    def generate_packets_array_data(self) -> List[bytes]:
        """Generate packets for array variable data only."""
        packets = []
        for m_type in sorted(self.storage.keys()):
            if not self.storage[m_type]: continue
            item_size = mem_types_size.get(m_type, 1)

            pkt_header = struct.pack('<BBB', self.ctx_id, m_type.value, 0)
            current_buffer = bytearray(pkt_header)
            count_in_pkt = 0
            for idx, inst in enumerate(self.storage[m_type]):
                if inst.head.dims_cnt == 0 or inst.data is None: continue
                full_data = inst.pack_data()
                total_items = len(full_data) // item_size
                items_processed = 0
                while items_processed < total_items:
                    chunk_overhead = 6
                    remaining = self.pkt_size - len(current_buffer)
                    max_data = remaining - chunk_overhead
                    if max_data < item_size:
                        current_buffer[2] = count_in_pkt
                        packets.append(bytes(current_buffer))
                        current_buffer = bytearray(pkt_header)
                        count_in_pkt = 0
                        remaining = self.pkt_size - len(current_buffer)
                        max_data = remaining - chunk_overhead
                    items_to_take = min(total_items - items_processed, max_data // item_size)
                    byte_start = items_processed * item_size
                    byte_end = byte_start + (items_to_take * item_size)
                    chunk_header = struct.pack('<HHH', idx, items_processed, items_to_take)
                    current_buffer.extend(chunk_header)
                    current_buffer.extend(full_data[byte_start:byte_end])
                    count_in_pkt += 1
                    items_processed += items_to_take
            if count_in_pkt > 0:
                current_buffer[2] = count_in_pkt
                packets.append(bytes(current_buffer))
        return packets
    
class Mem:
    def __init__(self, pkt_size: int = 512):
        self.contexts: List[mem_context_t] = [mem_context_t(i, pkt_size) for i in range(8)]
    
    def add_instance(self, ctx: int, type: mem_types_t, alias: str, data: Any = None, dims: Optional[List[int]] = None, can_clear: int = 1, is_standalone: int = 0, is_mutable: int = 1):
        """
        add for selected context new instance
        """
        if not (0 <= ctx < len(self.contexts)):
            raise IndexError(f"Context {ctx} out of range (0..{len(self.contexts)-1})")

        # ensure alias is not present in any context
        for c in self.contexts:
            if alias in c.alias_map:
                raise ValueError(f"Alias '{alias}' already exists in Context {c.ctx_id}")

        return self.contexts[ctx].add(type=type, alias=alias, data=data, dims=dims, can_clear=can_clear, is_standalone=is_standalone, is_mutable=is_mutable)
    
    def get_instance(self, alias: str):
        """Return the `mem_instance_t` matching `alias` from any context.
        Raises `ValueError` if the alias is not found.
        """
        for c in self.contexts:
            if alias in c.alias_map:
                return c.get_by_alias(alias)
        raise ValueError(f"Alias '{alias}' not found in any context")

    def remove_instance(self, alias: str) -> None:
        """Remove the instance matching `alias` from whichever context owns it.
        Raises `ValueError` if the alias is not found.
        """
        for c in self.contexts:
            if alias in c.alias_map:
                c.remove_by_alias(alias)
                return
        raise ValueError(f"Alias '{alias}' not found in any context")

class access_packet_t(ct.LittleEndianStructure):
    """Bitfield structure for instance access packet
    """
    _pack_ = 1
    _fields_ = [
        ("type",              ct.c_uint8, 4),
        ("ctx_id",            ct.c_uint8, 3),
        ("is_index_resolved", ct.c_uint8, 1),
        ("dims_cnt",          ct.c_uint8, 3),
        ("idx_type",          ct.c_uint8, 3),
        ("reserved",          ct.c_uint8, 2),
        ("instance_idx",      ct.c_uint16),
    ]

    
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

    def to_bytes(self, mem: Mem) -> bytes:

        instance = mem.get_instance(self.alias)

        # 2. Build header
        header = access_packet_t()
        header.type = instance.head.mem_type
        header.ctx_id = instance.head.context
        header.dims_cnt = instance.head.dims_cnt
        header.instance_idx= instance.my_index
        
        can_resolve = self._can_resolve()
        
        # Scalar (no dims) or all static indices -> resolved
        if len(self.indices) == 0:
            header.is_index_resolved = 1
        elif can_resolve:
            header.is_index_resolved = 1
        else:
            header.is_index_resolved = 0
        
        #Build mask (bit=1 means static uint16, bit=0 means dynamic Ref)
        idx_type_mask = 0
        for i, (is_static, _) in enumerate(self.indices):
            if is_static:
                idx_type_mask |= (1 << i)
        header.idx_type = idx_type_mask & 0x7
        
        # Convert header to bytes
        blob = bytearray(bytes(header))
        
        # Serialize indices payload
        for is_static, val in self.indices:
            if is_static:
                # Static: uint16_t value
                blob.extend(struct.pack('<H', val))
            else:
                # Dynamic: recursive access_packet
                blob.extend(val.to_bytes(mem))

        return bytes(blob)
    

 # vibecoding   
def ref_from_str(string: str, mem: Mem) -> Ref:
    """Parse a string into a Ref with nested index support.

    Syntax:
        "alias"                     -> Ref("alias")
        "alias[0][1]"               -> Ref("alias")[0, 1]
        "alias[other]"              -> Ref("alias")[Ref("other")]
        "alias[arr[0]][i[j]]"       -> Ref("alias")[Ref("arr")[0], Ref("i")[Ref("j")]]

    Each index inside [] is either:
      - a decimal integer  -> static index
      - another alias[...] -> dynamic Ref (recursive)
    """
    s = string.strip()
    ref, pos = _parse_ref(s, 0)
    if pos != len(s):
        raise ValueError(f"Unexpected trailing characters at position {pos}: '{s[pos:]}'")
    return ref


def _parse_ref(s: str, pos: int) -> Tuple[Ref, int]:
    """Parse 'alias' optionally followed by [idx][idx]... starting at *pos*.
    Returns (Ref, next_pos).
    """
    # 1. Read the alias name (everything up to '[' or end)
    start = pos
    while pos < len(s) and s[pos] not in '[]':
        pos += 1
    alias = s[start:pos].strip()
    if not alias:
        raise ValueError(f"Empty alias at position {start}")

    ref = Ref(alias)

    # 2. Consume zero or more [index] groups
    while pos < len(s) and s[pos] == '[':
        pos += 1  # skip '['
        idx, pos = _parse_index_content(s, pos)
        # Append index to ref
        if isinstance(idx, int):
            ref.indices.append((True, idx))
        else:
            ref.indices.append((False, idx))
        if pos >= len(s) or s[pos] != ']':
            raise ValueError(f"Expected ']' at position {pos}")
        pos += 1  # skip ']'

    return ref, pos


def _parse_index_content(s: str, pos: int) -> Tuple[Union[int, Ref], int]:
    """Parse the content inside [...].
    Can be an integer literal or a nested alias (possibly with its own indices).
    Returns (int_or_Ref, position_after_content_before_closing_bracket).
    """
    # skip whitespace
    while pos < len(s) and s[pos] == ' ':
        pos += 1

    if pos >= len(s):
        raise ValueError("Unexpected end of string inside index")

    # Try integer
    if s[pos].isdigit() or (s[pos] == '-' and pos + 1 < len(s) and s[pos + 1].isdigit()):
        start = pos
        if s[pos] == '-':
            pos += 1
        while pos < len(s) and s[pos].isdigit():
            pos += 1
        return int(s[start:pos]), pos

    # Otherwise it's a nested alias[...]
    ref, pos = _parse_ref(s, pos)
    return ref, pos



