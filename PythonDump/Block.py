import ctypes as ct
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Any
from Enums import block_types_t, mem_types_t, packet_header_t
from MemAcces import Ref, AccessManager
from Mem import mem_context_t

# ============================================================================
# 1. C-COMPATIBLE STRUCTURES
# ============================================================================

class block_cfg_t(ct.LittleEndianStructure):
    """
    Maps to C struct:
    __packed struct {
        uint16_t block_idx;          /*index of block in code*/
        uint16_t in_connected_mask;  /*connected inputs (those that have instance)*/
        uint8_t block_type;          /*typeof block (function)*/
        uint8_t in_cnt;              /*count of inputs*/
        uint8_t q_cnt;               /*count of outputs*/
    } cfg;
    
    Total: 7 bytes
    """
    _pack_ = 1
    _fields_ = [
        ("block_idx",          ct.c_uint16),
        ("in_connected_mask",  ct.c_uint16),
        ("block_type",         ct.c_uint8),
        ("in_cnt",             ct.c_uint8),
        ("q_cnt",              ct.c_uint8),
    ]

# ============================================================================
# 2. OUTPUT PROXY - For easy output referencing
# ============================================================================

class BlockOutputProxy:
    """
    Proxy class to access block outputs by index.
    
    Outputs are named as "{block_idx}\_q\_{output_index}".

    Usage:
        block.out[0]           # Returns Ref to output 0
        block.out[1][0, 0]     # Returns Ref to output 1 with array indices
    """
    
    def __init__(self, block_idx: int):
        self.block_idx = block_idx
    
    def __getitem__(self, idx: int) -> Ref:
        """Get Ref for output at given index."""
        alias = f"{self.block_idx}_q_{idx}"
        return Ref(alias)

# ============================================================================
# 3. BLOCK CLASS
# ============================================================================

@dataclass
class Block:
    """
    Represents a block in the emulator code.
    
    - idx: Block index in code (user provided for now, later auto-assigned)
    - block_type: Type of block (from blck_types_t enum)
    - ctx: Context where output variables are created
    - in_conn: List of input connections (Refs)
    - q_conn: List of output connections (Refs, auto-created)
    - out: Proxy for accessing outputs as Refs (e.g., block.out[0])
    """
    idx: int
    block_type: block_types_t
    ctx: mem_context_t
    in_conn: List[Optional[Ref]] = field(default_factory=list)
    q_conn: List[Optional[Ref]] = field(default_factory=list)
    
    # Track which inputs are connected (for mask generation)
    _in_connected: List[bool] = field(default_factory=list)
    
    def __init__(self, idx: int, block_type: block_types_t, ctx: mem_context_t):
        self.idx = idx
        self.block_type = block_type
        self.ctx = ctx
        self.in_conn = []
        self.q_conn = []
        self._in_connected = []
        self.out = BlockOutputProxy(idx)
    
    def add_inputs(self, inputs: List[Optional[Ref]]) -> 'Block':
        """
        Add input connections.
        
        :param inputs: List of Ref objects or None for unconnected inputs
        :return: self for chaining
        """
        for inp in inputs:
            if inp is not None:
                self.in_conn.append(inp)
                self._in_connected.append(True)
            else:
                self.in_conn.append(None)
                self._in_connected.append(False)
        return self
    
    def _add_output(self, 
                    q_type: mem_types_t, 
                    dims: Optional[List[int]] = None,
                    data: Any = None) -> Ref:
        """
        Internal: Create an output variable and its reference.
        
        Output variable is created in the context with name "{idx}_q_{q_num}".
        For arrays, indices default to zero.
        
        NOTE: This is for internal use by block subclasses. Users should not
        call this directly - each block type auto-creates its outputs.
        
        :param q_type: Memory type for the output variable
        :param dims: Dimensions for array output (None for scalar)
        :param data: Initial data value
        :return: Reference to the created output
        """
        q_num = len(self.q_conn)
        alias = f"{self.idx}_q_{q_num}"
        
        # Create variable in context
        self.ctx.add(
            type=q_type,
            alias=alias,
            dims=dims,
            data=data,
            can_clear=1  # Outputs can have updated flag cleared
        )
        
        # Create reference
        ref = Ref(alias)
        
        # For arrays, add zero indices
        if dims is not None and len(dims) > 0:
            zero_indices = [0] * len(dims)
            ref = ref[tuple(zero_indices)]
        
        self.q_conn.append(ref)
        return ref
    
    def _add_outputs(self, 
                     output_specs: List[tuple]) -> List[Ref]:
        """
        Internal: Add multiple outputs at once.
        
        :param output_specs: List of (q_type,) or (q_type, dims) or (q_type, dims, data) tuples
        :return: List of created Refs
        """
        refs = []
        for spec in output_specs:
            if len(spec) == 1:
                refs.append(self._add_output(spec[0]))
            elif len(spec) == 2:
                refs.append(self._add_output(spec[0], spec[1]))
            else:
                refs.append(self._add_output(spec[0], spec[1], spec[2]))
        return refs
    
    def get_connected_mask(self) -> int:
        """Generate bitmask for connected inputs."""
        mask = 0
        for i, connected in enumerate(self._in_connected):
            if connected:
                mask |= (1 << i)
        return mask
    
    def pack_cfg(self) -> bytes:
        """Pack block configuration to bytes."""
        cfg = block_cfg_t()
        cfg.block_idx = self.idx
        cfg.in_connected_mask = self.get_connected_mask()
        cfg.block_type = self.block_type.value
        cfg.in_cnt = len(self.in_conn)
        cfg.q_cnt = len(self.q_conn)
        return bytes(cfg)
    
    def pack_inputs(self, manager: Optional[AccessManager] = None) -> List[bytes]:
        """
        Pack each connected input reference as separate packet.
        Format per packet: [header:u8][block_idx:u16][in_num:u8][access_node]
        Returns list of packets, one per connected input.
        """
        if manager is None:
            manager = AccessManager.get_instance()
        
        packets = []
        in_num = 0
        for inp in self.in_conn:
            if inp is not None:
                # Header: packet type + block_idx + input number
                header = struct.pack('<BHB', 
                    packet_header_t.PACKET_H_BLOCK_INPUTS,
                    self.idx,
                    in_num
                )
                packets.append(header + inp.to_bytes(manager))
            in_num += 1
        return packets
    
    def pack_outputs(self, manager: Optional[AccessManager] = None) -> List[bytes]:
        """
        Pack each output reference as separate packet.
        Format per packet: [header:u8][block_idx:u16][q_num:u8][access_node]
        Returns list of packets, one per output.
        """
        if manager is None:
            manager = AccessManager.get_instance()
        
        packets = []
        for q_num, out in enumerate(self.q_conn):
            # Header: packet type + block_idx + output number
            header = struct.pack('<BHB',
                packet_header_t.PACKET_H_BLOCK_OUTPUTS,
                self.idx,
                q_num
            )
            packets.append(header + out.to_bytes(manager))
        return packets
    
    def __repr__(self) -> str:
        return (f"Block(idx={self.idx}, type={self.block_type.name}, "
                f"in={len(self.in_conn)}, q={len(self.q_conn)})")

