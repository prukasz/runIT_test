import ctypes as ct
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Any
from Enums import block_types_t, mem_types_t, packet_header_t
from Mem import Mem, Ref, mem_instance_t
from Mem import mem_context_t

class block_cfg_t(ct.LittleEndianStructure):
    """Represents block_cfg_t in C """
    _pack_ = 1
    _fields_ = [
        ("block_idx",          ct.c_uint16),
        ("in_connected_mask",  ct.c_uint16),
        ("block_type",         ct.c_uint8),
        ("in_cnt",             ct.c_uint8),
        ("q_cnt",              ct.c_uint8),
    ]

class BlockOutputProxy:
    """Proxy class to access block outputs by index and class"""
    
    def __init__(self, q_instances: list[Optional[mem_instance_t]]):
        self.q_instances = q_instances 
    def __getitem__(self, idx):
        """Access output by index or by multi-index tuple.

        Examples:
            out[2]            -> Ref for output 2
            out[2, 1, 3]      -> Ref for output 2 indexed [1,3]
        """
        # normalize index to primary + rest
        if isinstance(idx, (list, tuple)):
            if len(idx) == 0:
                raise IndexError("Empty index")
            primary = idx[0]
            rest = tuple(idx[1:]) if len(idx) > 1 else ()
        else:
            primary = idx
            rest = ()

        try:
            inst = self.q_instances[primary]
        except Exception:
            raise IndexError(f"Output index {primary} out of range")

        if inst is None:
            raise KeyError(f"No output instance connected at index {primary}")

        ref = Ref(inst.alias)
        return ref[rest] if rest else ref

@dataclass
class Block:
    """Represents a block in the emulator code."""
    
    in_conn: List[Ref]
    q_conn: List[Ref]

    def __init__(self, idx: int, alias: str, block_type: block_types_t, ctx_id, mem: Mem):
        self.cfg = block_cfg_t()
        self.cfg.block_idx = idx
        self.cfg.block_type = block_type.value
        self.ctx_id = ctx_id
        self.mem = mem

        self.alias = alias
        self.in_conn = []
        self.q_conn = []
        self._in_connected = []
        self._q_instances = []
        self.out = BlockOutputProxy(self._q_instances)
    
    def add_inputs(self, inputs: List[Optional[Ref]]):
        """ Input is always reference """    
        for inp in inputs:
            if inp is not None:
                self.in_conn.append(inp)
                self._in_connected.append(True)
            else:
                self.in_conn.append(None)
                self._in_connected.append(False)
        # do not overwrite `self.in_conn` list; record count in cfg
        self.cfg.in_cnt = len(inputs)
        self.cfg.in_connected_mask = self.get_connected_mask()
        return self
    
    def add_outputs(self, outputs: List[tuple]):
        """List[mem_type_t, opt(dims)]"""
        # record number of outputs
        self.cfg.q_cnt = len(outputs)

        for i, output in enumerate(outputs):
            # expect output == (mem_type, dims_or_None)
            m_type = output[0]
            dims = output[1] if len(output) > 1 else None

            new_inst = self.mem.add_instance(self.ctx_id, m_type, f"{self.alias}_q_{i}", dims=dims, can_clear=1, is_standalone=0, is_mutable=0)
            self._q_instances.append(new_inst)

            if dims:
                # pass a tuple of zeros for indexing (Ref.__getitem__ expects tuple)
                zero_indices = tuple(0 for _ in dims)
                self.q_conn.append(Ref(new_inst.alias)[zero_indices])
            else:
                self.q_conn.append(Ref(new_inst.alias))
    
    def get_connected_mask(self) -> int:
        """Generate bitmask for connected inputs."""
        mask = 0
        for i, connected in enumerate(self._in_connected):
            if connected:
                mask |= (1 << i)
        return mask
    

    def pack_cfg(self) -> bytes:

        return struct.pack("<B", packet_header_t.PACKET_H_BLOCK_HEADER.value ) + bytes(self.cfg)


    def pack_connections(self) -> List[bytes]:
        """
        Pack each connected input or output reference
        NEW!!!! one packet type for connectiont, distinct by byte after block_idx
        input 0x00, output 0x01
        """
        packets: List[bytes] = []

        def _append_conn(conn_list: List[Optional[Ref]], kind_flag: int, pkt_type) -> None:
            for idx, node in enumerate(conn_list):
                if node is None:
                    continue
                header = struct.pack('<BHBB', pkt_type, self.cfg.block_idx, kind_flag, idx)
                packets.append(header + node.to_bytes(self.mem))

        # inputs: kind_flag=0, outputs: kind_flag=1
        _append_conn(self.in_conn, 0x00, packet_header_t.PACKET_H_BLOCK_INPUTS)
        _append_conn(self.q_conn, 0x01, packet_header_t.PACKET_H_BLOCK_OUTPUTS)

        return packets


    #helper
    def get_my_instance(self, out_num: int) -> mem_instance_t:
        """Helper - get instance of selected block output"""
        if out_num < 0 or out_num >= len(self._q_instances):
            raise IndexError(f"Output number {out_num} out of range for block with {len(self._q_instances)} outputs")
        inst = self._q_instances[out_num]
        if inst is None:
            raise ValueError(f"Output {out_num} of block has no instance connected")
        return inst
    
    #helper
    def get_my_output_ref(self, out_num: int) -> Ref:
        """Get Ref of selected block output"""
        if out_num < 0 or out_num >= len(self._q_instances):
            raise IndexError(f"Output number {out_num} out of range for block with {len(self._q_instances)} outputs")
        ref = self.q_conn[out_num]
        if ref is None:
            raise ValueError(f"Output {out_num} of block has no instance connected")
        return ref