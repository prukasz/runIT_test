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
        return bytes(self.cfg)

    def pack_inputs(self) -> List[bytes]:
        """
        Pack each connected input reference as separate packet.
        Format per packet: [header:u8][block_idx:u16][in_num:u8][access_node]
        Returns list of packets, one per connected input.
        """
        packets = []
        for in_num, inp in enumerate(self.in_conn):
            if inp is not None:
                header = struct.pack('<BHB', 
                    packet_header_t.PACKET_H_BLOCK_INPUTS,
                    self.cfg.block_idx,
                    in_num
                )
                packets.append(header + inp.to_bytes(self.mem))
        return packets

    def pack_outputs(self) -> List[bytes]:
        """
        Pack each output reference as separate packet.
        Format per packet: [header:u8][block_idx:u16][q_num:u8][access_node]
        Returns list of packets, one per output.
        """
        packets = []
        for q_num, out in enumerate(self.q_conn):
            header = struct.pack('<BHB',
                packet_header_t.PACKET_H_BLOCK_OUTPUTS,
                self.cfg.block_idx,
                q_num
            )
            packets.append(header + out.to_bytes(self.mem))
        return packets

    def __repr__(self) -> str:
        return (f"Block(idx={self.cfg.block_idx}, type={self.cfg.block_type}, "
                f"in={len(self.in_conn)}, q={len(self.q_conn)})")


# ============================================================================
# Quick smoke test
# ============================================================================
if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(__file__))

    mem = Mem()

    # --- create a block ---
    b = Block(idx=0, alias="test_blk", block_type=block_types_t.BLOCK_MATH, ctx_id=0, mem=mem)

    # add two scalar outputs
    b.add_outputs([(mem_types_t.MEM_U16,), (mem_types_t.MEM_U16,)])
    assert len(b._q_instances) == 2, "Expected 2 output instances"
    assert b.cfg.q_cnt == 2

    # add inputs: one connected, one disconnected
    ref0 = Ref(b._q_instances[0].alias)
    b.add_inputs([ref0, None])
    assert b.cfg.in_cnt == 2
    assert b.get_connected_mask() == 0b01  # only input 0 connected

    # pack cfg
    cfg_bytes = b.pack_cfg()
    assert len(cfg_bytes) == ct.sizeof(block_cfg_t), f"cfg size mismatch: {len(cfg_bytes)}"

    # proxy access
    out_ref = b.out[0]
    assert isinstance(out_ref, Ref)
    assert out_ref.alias == b._q_instances[0].alias

    # repr
    print(repr(b))
    print(f"cfg bytes  : {cfg_bytes.hex()}")
    print(f"pack_inputs: {len(b.pack_inputs())} packets")
    print(f"pack_outputs: {len(b.pack_outputs())} packets")
    print("ALL TESTS PASSED") 
    

    
    

