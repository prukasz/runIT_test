"""
Separate Linked Accessor Generator.
Manages relational accessors linked to VM objects, resolves dependencies,
assigns accessor IDs automatically at compile time, and builds Packet 0x44.
"""
from __future__ import annotations
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Set, Tuple, Union, TYPE_CHECKING

from .vm_types import (
    CLASS_VM_LOADER,
    HEADER_PKT_VM_ADD_ACC,
    VMIndexKind,
)

if TYPE_CHECKING:
    from .object import VMObject


@dataclass
class IndexStep:
    """One indexing step in an Accessor chain."""
    kind: VMIndexKind
    value: Union[int, "Accessor", str]

    def encode(self) -> bytes:
        """
        Encodes this step into wire bytes:
        - VM_IDX_LITERAL: [0x00, u32 value] (5 bytes)
        - VM_IDX_REF:     [0x01, u16 target_acc_id] (3 bytes)
        - VM_IDX_NAME:    [0x02, u8 name_len, ASCII bytes]
        """
        if self.kind == VMIndexKind.LITERAL:
            return struct.pack("<BI", int(VMIndexKind.LITERAL), int(self.value))
        elif self.kind == VMIndexKind.REF:
            ref_acc: "Accessor" = self.value  # type: ignore
            if ref_acc.id is None:
                raise ValueError(f"Referenced accessor {ref_acc} has not been assigned an ID!")
            return struct.pack("<BH", int(VMIndexKind.REF), int(ref_acc.id))
        elif self.kind == VMIndexKind.NAME:
            name_bytes = str(self.value).encode("ascii", errors="replace")
            if len(name_bytes) > 255:
                name_bytes = name_bytes[:255]
            return struct.pack("<BB", int(VMIndexKind.NAME), len(name_bytes)) + name_bytes
        else:
            raise ValueError(f"Unsupported index step kind: {self.kind}")


class Accessor:
    """
    Represents a typed, indexed accessor linked to a root VMObject.

    Rules:
    - `id` is NOT set manually. It is assigned automatically at compile time
      by the AccessorGenerator.
    - Linked directly to its root VMObject relation.
    - Contains 0 or more index steps (e.g. obj[0], obj[ref_acc], obj['tag']).
    """

    def __init__(self, root_obj: "VMObject", steps: Optional[List[IndexStep]] = None):
        self.root_obj = root_obj
        self.steps: List[IndexStep] = list(steps) if steps else []
        self.id: Optional[int] = None

    def at(self, index: Union[int, "Accessor", str]) -> "Accessor":
        """Chains another index step and returns a new Accessor relation."""
        new_steps = list(self.steps)
        if isinstance(index, int):
            new_steps.append(IndexStep(kind=VMIndexKind.LITERAL, value=index))
        elif isinstance(index, Accessor):
            new_steps.append(IndexStep(kind=VMIndexKind.REF, value=index))
        elif isinstance(index, str):
            new_steps.append(IndexStep(kind=VMIndexKind.NAME, value=index))
        else:
            raise TypeError(f"Invalid index type for Accessor: {type(index)}")
        return Accessor(self.root_obj, new_steps)

    def __getitem__(self, index: Union[int, "Accessor", str]) -> "Accessor":
        return self.at(index)

    @property
    def key(self) -> Tuple[Any, ...]:
        """Unique structural signature used for deduplication."""
        step_keys = []
        for s in self.steps:
            if s.kind == VMIndexKind.REF:
                step_keys.append((s.kind, id(s.value)))
            else:
                step_keys.append((s.kind, s.value))
        return (id(self.root_obj), tuple(step_keys))

    def encode_indices(self) -> bytes:
        """Packs all chained index steps into raw wire bytes."""
        out = bytearray()
        for step in self.steps:
            out.extend(step.encode())
        return bytes(out)

    def __repr__(self) -> str:
        id_str = f"id={self.id}" if self.id is not None else "id=UNASSIGNED"
        steps_str = "".join(f"[{s.value}]" for s in self.steps)
        obj_name = self.root_obj.name or f"obj_{self.root_obj.id}"
        return f"<Accessor {id_str} -> {obj_name}{steps_str}>"


class AccessorGenerator:
    """
    Dedicated generator for resolving, deduplicating, ordering, and
    compiling accessors into Packet 0x44 (Add Accessors Batch).
    """

    def __init__(self):
        self._accessors: List[Accessor] = []
        self._seen_keys: Dict[Tuple[Any, ...], Accessor] = {}

    def register(self, accessor: Accessor) -> Accessor:
        """
        Registers an accessor. If an identical structural accessor already
        exists, the canonical existing instance is returned (deduplication).
        """
        key = accessor.key
        if key in self._seen_keys:
            return self._seen_keys[key]

        # Register any prerequisite accessors referenced by REF steps
        for step in accessor.steps:
            if step.kind == VMIndexKind.REF and isinstance(step.value, Accessor):
                step.value = self.register(step.value)

        self._seen_keys[key] = accessor
        self._accessors.append(accessor)
        return accessor

    def compile(self) -> List[Accessor]:
        """
        Resolves dependencies and automatically assigns sequential IDs (0, 1, 2, ...).
        Uses topological sort to ensure accessors referenced via VM_IDX_REF
        are assigned lower IDs and appear before the accessors that reference them.
        """
        # Build dependency graph
        dependencies: Dict[Accessor, Set[Accessor]] = {acc: set() for acc in self._accessors}
        for acc in self._accessors:
            for step in acc.steps:
                if step.kind == VMIndexKind.REF and isinstance(step.value, Accessor):
                    dependencies[acc].add(step.value)

        # Topological sort (Kahn's algorithm)
        in_degree = {acc: len(deps) for acc, deps in dependencies.items()}
        queue = [acc for acc, deg in in_degree.items() if deg == 0]
        ordered: List[Accessor] = []

        while queue:
            node = queue.pop(0)
            ordered.append(node)
            for acc, deps in dependencies.items():
                if node in deps:
                    in_degree[acc] -= 1
                    if in_degree[acc] == 0:
                        queue.append(acc)

        if len(ordered) != len(self._accessors):
            raise RuntimeError("Circular reference detected among accessors!")

        # Automatically assign IDs
        for idx, acc in enumerate(ordered):
            acc.id = idx

        self._accessors = ordered
        return ordered

    def build_record(self, acc: Accessor) -> bytes:
        """
        Builds a single accessor binary record for Packet 0x44:
        - u16 acc_id
        - u16 root_obj_id
        - u8  idx_count
        - u8  idx_len
        - u8  idx_data[idx_len]
        """
        if acc.id is None:
            raise ValueError(f"Accessor {acc} has no assigned ID!")
        if acc.root_obj.id is None:
            raise ValueError(f"Root object for accessor {acc} has no assigned ID!")

        idx_data = acc.encode_indices()
        idx_count = len(acc.steps)
        idx_len = len(idx_data)

        if idx_count > 255 or idx_len > 255:
            raise ValueError(f"Accessor index depth or bytes exceeds 255 (count={idx_count}, len={idx_len})")

        header = struct.pack("<HHBB", acc.id, acc.root_obj.id, idx_count, idx_len)
        return header + idx_data

    def generate_packets(self, max_packet_size: int = 240) -> List[bytes]:
        """
        Generates one or more wire-compatible Packet 0x44 frames.
        Layout: [0x04, 0x44, count(u8), records...]
        Splits records across frames if count > 255 or payload exceeds max_packet_size.
        """
        if not self._accessors:
            return []

        packets: List[bytes] = []
        current_records: List[bytes] = []
        current_len = 3  # class (1) + pkt (1) + count (1)

        for acc in self._accessors:
            rec = self.build_record(acc)
            if current_records and (current_len + len(rec) > max_packet_size or len(current_records) >= 255):
                # Emit current frame
                frame = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_ADD_ACC, len(current_records)])
                for r in current_records:
                    frame.extend(r)
                packets.append(bytes(frame))

                current_records = []
                current_len = 3

            current_records.append(rec)
            current_len += len(rec)

        if current_records:
            frame = bytearray([CLASS_VM_LOADER, HEADER_PKT_VM_ADD_ACC, len(current_records)])
            for r in current_records:
                frame.extend(r)
            packets.append(bytes(frame))

        return packets

    def __len__(self) -> int:
        return len(self._accessors)

    def __iter__(self):
        return iter(self._accessors)
