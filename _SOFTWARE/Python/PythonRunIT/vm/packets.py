"""
Binary packet encoders for VM Loader protocol (Class 0x04).
Matches components/codecs/decoders/dec_vm_loader.h.
"""
from __future__ import annotations
import struct
from typing import List, Optional, Tuple, Union

from .vm_types import (
    CLASS_VM_LOADER,
    HEADER_PKT_VM_RESET,
    HEADER_PKT_VM_OPEN,
    HEADER_PKT_VM_ADD_OBJS,
    HEADER_PKT_VM_SET_DATA,
    HEADER_PKT_VM_ADD_ACC,
    HEADER_PKT_VM_SUBSCRIBE,
    HEADER_PKT_VM_EXEC,
    VMExecCommand,
)


def encode_vm_reset() -> bytes:
    """
    Packet 0x40: VM Reset
    Tears down running programs, clears store registries and arena, stops execution.
    """
    return bytes([CLASS_VM_LOADER, HEADER_PKT_VM_RESET])


def encode_vm_open(obj_cnt: int, acc_cnt: int, blk_cnt: int, total_size: int) -> bytes:
    """
    Packet 0x41: Open Program Container
    Declares capacity for objects, accessors, blocks, and memory arena footprint in bytes.
    12 bytes total: [0x04, 0x41, obj_cnt(u16), acc_cnt(u16), blk_cnt(u16), total_size(u32)]
    """
    return struct.pack(
        "<BBHHHI",
        CLASS_VM_LOADER,
        HEADER_PKT_VM_OPEN,
        int(obj_cnt),
        int(acc_cnt),
        int(blk_cnt),
        int(total_size),
    )


def encode_vm_add_objs(
    records: List[Tuple[int, bytes, Optional[bytes]]],
    max_packet_size: int = 240,
) -> List[bytes]:
    """
    Packet 0x42: Add Objects Batch
    Records: List of (obj_id: int, head_bytes: 4 bytes, name_bytes: Optional[bytes]).
    Framing: [0x04, 0x42, count(u8), records...]
    """
    if not records:
        return []

    packets: List[bytes] = []
    current_recs: List[bytes] = []
    current_len = 3  # class + pkt + count

    for obj_id, head_bytes, name_bytes in records:
        rec = bytearray(struct.pack("<H", obj_id))
        rec.extend(head_bytes)
        if name_bytes:
            rec.extend(name_bytes)

        if current_recs and (current_len + len(rec) > max_packet_size or len(current_recs) >= 255):
            pkt = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_ADD_OBJS, len(current_recs)])
            for r in current_recs:
                pkt.extend(r)
            packets.append(bytes(pkt))
            current_recs = []
            current_len = 3

        current_recs.append(bytes(rec))
        current_len += len(rec)

    if current_recs:
        pkt = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_ADD_OBJS, len(current_recs)])
        for r in current_recs:
            pkt.extend(r)
        packets.append(bytes(pkt))

    return packets


def encode_vm_set_data(
    records: List[Tuple[int, int, bytes]],
    max_packet_size: int = 240,
) -> List[bytes]:
    """
    Packet 0x43: Set Object Initial Data / Override
    Records: List of (obj_id: int, start_idx: int, data_bytes: bytes).
    Framing: [0x04, 0x43, count(u8), records...]
    Each record: [obj_id(u16), start_idx(u16), byte_len(u16), data(byte_len bytes)]
    """
    if not records:
        return []

    packets: List[bytes] = []
    current_recs: List[bytes] = []
    current_len = 3

    for obj_id, start_idx, data_bytes in records:
        rec = bytearray(struct.pack("<HHH", obj_id, start_idx, len(data_bytes)))
        rec.extend(data_bytes)

        if current_recs and (current_len + len(rec) > max_packet_size or len(current_recs) >= 255):
            pkt = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_SET_DATA, len(current_recs)])
            for r in current_recs:
                pkt.extend(r)
            packets.append(bytes(pkt))
            current_recs = []
            current_len = 3

        current_recs.append(bytes(rec))
        current_len += len(rec)

    if current_recs:
        pkt = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_SET_DATA, len(current_recs)])
        for r in current_recs:
            pkt.extend(r)
        packets.append(bytes(pkt))

    return packets


def encode_vm_subscribe(obj_ids: List[int]) -> bytes:
    """
    Packet 0x47: Subscribe Live Telemetry
    Framing: [0x04, 0x47, count(u8), obj_id_0(u16), obj_id_1(u16), ...]
    """
    pkt = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_SUBSCRIBE, len(obj_ids)])
    for oid in obj_ids:
        pkt.extend(struct.pack("<H", oid))
    return bytes(pkt)


def encode_vm_exec(command: Union[int, VMExecCommand]) -> bytes:
    """
    Packet 0x48: Runtime Execution Control
    Framing: [0x04, 0x48, command(u8)]
    """
    return bytes([CLASS_VM_LOADER, HEADER_PKT_VM_EXEC, int(command)])
