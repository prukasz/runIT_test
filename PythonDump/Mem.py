import ctypes as ct
from enum import IntEnum
from dataclasses import dataclass, field
from typing import Any, List, Dict, Optional, Tuple
import struct
# Local Imports 
""" mem_types_map  maps mem_types_t to ctypes types
    mem_types_size maps mem_types_t to their sizes in bytes (sizof(ctype))
    mem_types_pack_map maps mem_types_t to struct pack format strings
    packet_header_t defines packet header enums
"""
from Enums import mem_types_t, mem_types_map, mem_types_pack_map, mem_types_size, packet_header_t

# ============================================================================
# 1. Enums & Structures
# ============================================================================


class instance_head_t(ct.Structure):
    """ instance_head_t: Bitfield structure for memory instance header this is given in packet to describe instance
    This is similar to C struct with bitfields but no data pointers, and dims are stored separately also

    uint16_t context   : 3;  /*Context that instance shall belong to*/
    uint16_t dims_cnt  : 4;  /*dimensions count in case of arrays > 0*/
    uint16_t type      : 4;  /*mem_types_t type*/
    uint16_t updated   : 1;  /*Updated flag can be used for block output variables*/
    uint16_t can_clear : 1;  /*Can updated flag be cleared*/
    uint16_t reserved  : 3;  /*padding*/
"""
    _pack_ = 1
    _fields_ = [
        ("context",       ct.c_uint16, 3),
        ("dims_cnt",      ct.c_uint16, 4),
        ("type",          ct.c_uint16, 4),
        ("updated",       ct.c_uint16, 1),
        ("can_clear",     ct.c_uint16, 1),
        ("reserved",      ct.c_uint16, 3),
    ]




@dataclass 
class mem_instance_t:
    """
    mem_instance_t: Represents a memory instance with header, dimensions, and data, stored in python and recreated in C side separately
    alias is only for python side to refer to this instance by name, instance index is stored 
    """

    head: instance_head_t
    dims: List[ct.c_uint16] #list of dimensions as in C
    data: Any #any of datatype + list or not 

    # Metadata for Python side only, we give reference python side
    alias: Optional[str] = None

    #index of this instance in context storage list for its type (must be equal to emulator side)
    my_index: int = 0 

    def __init__(self, ctx: int, type: mem_types_t, dims: Optional[List[int]], data: Any, can_clear: int = 1, alias: str = None, idx: int = 0):
        self.head = instance_head_t()
        self.head.context = ctx
        self.head.type = type.value
        self.head.can_clear = can_clear
        self.head.updated = 1
        
        safe_dims = dims if dims is not None else []
        self.head.dims_cnt = len(safe_dims)
        self.dims = [ct.c_uint16(d) for d in safe_dims]
        self.data = data
        self.alias = alias
        self.my_index = idx

    def pack(self) -> bytes:
        """Packs Configuration (Header + Dims)"""
        count = len(self.dims)
        self.head.dims_cnt = count
        header_bytes = bytes(self.head)
        dims_bytes = b''
        if count > 0:
            ArrayType = ct.c_uint16 * count
            raw_values = [d.value if hasattr(d, 'value') else int(d) for d in self.dims]
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

# ============================================================================
# 3. Context Manager
# ============================================================================
@dataclass
class mem_context_t:
    """ mem_context_t: Manages memory instances within a context, with aliasing support 
    ctx id same as in C side
    pkt size is for packet generation only
    storage is a dict of lists of mem_instance_t for each mem_types_t type
    alias_map is a registry to map alias strings to (Type, Index) tuples
    """

    ctx_id: int
    pkt_size: int = 512
    storage: Dict[mem_types_t, List[mem_instance_t]] = field(default_factory=dict)
    # Registry to map alias -> (Type, Index)
    alias_map: Dict[str, Tuple[mem_types_t, int]] = field(default_factory=dict)

    def __init__(self, ctx_id: int, pkt_size: int = 512):
        self.ctx_id = ctx_id
        self.pkt_size = pkt_size
        self.storage = {t: [] for t in mem_types_t}
        self.alias_map = {}

    def add(self, type: mem_types_t, alias: str, data: Any = None, dims: Optional[List[int]] = None, can_clear: int = 1):
        """
        Adds an instance to the context.
        :param alias: Unique string name to refer to this variable later.
        """
        # 1. Calculate the next index for this type
        current_list = self.storage[type]
        next_idx = len(current_list)

        # 2. Create Instance
        new_inst = mem_instance_t(
            ctx=self.ctx_id,
            type=type,
            dims=dims,
            data=data,
            can_clear=can_clear,
            alias=alias,
            idx=next_idx
        )
        
        # 3. Store
        current_list.append(new_inst)
        
        # 4. Register Alias
        if alias in self.alias_map:
            raise ValueError(f"Alias '{alias}' already exists in Context {self.ctx_id}")
        self.alias_map[alias] = (type, next_idx)
        
        return new_inst

    def get_ref(self, alias: str) -> Tuple[mem_types_t, int]:
        """Returns (Type, Index) for a given alias."""
        if alias not in self.alias_map:
            raise KeyError(f"Alias '{alias}' not found in Context {self.ctx_id}")
        return self.alias_map[alias]

    # ... [generate_packets_instances SAME AS BEFORE] ...
    def generate_packets_instances(self) -> List[bytes]:
        packets = []
        for m_type in sorted(self.storage.keys()):
            current_buffer = bytearray()
            for inst in self.storage[m_type]:
                instance_bytes = inst.pack()
                inst_len = len(instance_bytes)
                if inst_len > self.pkt_size: raise ValueError("Instance too big")
                
                if len(current_buffer) + inst_len > self.pkt_size:
                    packets.append(bytes(current_buffer))
                    current_buffer = bytearray()
                current_buffer.extend(instance_bytes)
            
            if len(current_buffer) > 0: packets.append(bytes(current_buffer))
        return packets


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

    def generate_packets_data(self) -> List[bytes]:
        """Generate all data packets (scalars first, then arrays). Legacy wrapper."""
        return self.generate_packets_scalar_data() + self.generate_packets_array_data()

    # ... [packet_generate_cfg SAME AS BEFORE] ...
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


# ============================================================================
# 4. Usage Example
# ============================================================================
if __name__ == "__main__":
    ctx = mem_context_t(ctx_id=1)

    # 1. Add instances with ALIASES
    ctx.add(mem_types_t.MEM_F,   alias="temperature", data=25.5)
    ctx.add(mem_types_t.MEM_F,   alias="humidity",    data=60.2)
    ctx.add(mem_types_t.MEM_U16, alias="matrix",      dims=[10, 5], data=None)

    # 2. Check registry
    print("Alias Registry:", ctx.alias_map)
    # Output: {'temperature': (<mem_types_t.MEM_F: 6>, 0), 'humidity': (<mem_types_t.MEM_F: 6>, 1), 'matrix': (<mem_types_t.MEM_U16: 1>, 0)}

    # 3. Resolve alias
    m_type, idx = ctx.get_ref("humidity")
    print(f"Humidity is Type {m_type.name} at Index {idx}")
    
    # 4. Verify Index stored in instance
    inst = ctx.storage[m_type][idx]
    print(f"Verify Instance Metadata: Alias='{inst.alias}', Index={inst.my_index}")
