"""
VM Object definition with relational indexing and lazy ID assignment.
"""
from __future__ import annotations
import struct
from typing import Any, List, Optional, Union, TYPE_CHECKING
from .vm_types import (
    VMType,
    TYPE_SIZES,
    TYPE_STRUCT_FORMATS,
    VMFlags,
    VM_OBJ_NAME_MAX,
)

if TYPE_CHECKING:
    from .accessor_generator import Accessor


class VMObject:
    """
    Represents an object/variable in the VM store.

    Rules:
    - IDs are NOT set manually by the user; `id` is None until assigned
      automatically during compilation/packet generation.
    - Names are optional:
      - When named (e.g. user variables), `name` is tagged up to 15 chars.
      - When anonymous (e.g. intermediate block buffers), `name` is None/empty,
        and `name_size = 0` to save transmission bytes and memory.
    - Indexing (`obj[0]`, `obj[i]`, `obj.ref()`) produces a linked Accessor relation.
    """

    def __init__(
        self,
        type: VMType,
        count: int = 1,
        name: Optional[str] = None,
        initial: Optional[Any] = None,
        children: Optional[List["VMObject"]] = None,
        mutable: bool = True,
        retentive: bool = False,
        usr_protected: bool = False,
        upd_resetable: Optional[bool] = None,
    ):
        if children is not None:
            type = VMType.PTR
            count = len(children)
            self.children = list(children)
        else:
            self.children = []

        if count <= 0:
            raise ValueError(f"Object item count must be >= 1, got {count}")

        self.type = VMType(type)
        self.count = count
        self.name = name[:VM_OBJ_NAME_MAX] if name else None
        self.initial_value = initial
        self.mutable = mutable
        self.retentive = retentive
        self.usr_protected = usr_protected
        # Automatically deduced if not explicitly set:
        # Constants (mutable=False) retain permanent standing freshness (upd_resetable=False).
        # Variables (mutable=True) reset freshness each cycle (upd_resetable=True).
        self._explicit_upd_resetable = (upd_resetable is not None)
        self.upd_resetable = self.mutable if upd_resetable is None else bool(upd_resetable)

        # Assigned strictly at compile time!
        self.id: Optional[int] = None

    @property
    def item_size(self) -> int:
        return TYPE_SIZES.get(self.type, 0)

    @property
    def payload_size(self) -> int:
        return self.count * self.item_size

    @property
    def name_bytes(self) -> bytes:
        if not self.name:
            return b""
        return self.name.encode("ascii", errors="replace")[:VM_OBJ_NAME_MAX]

    @property
    def name_size(self) -> int:
        return len(self.name_bytes)

    def is_tagged(self) -> bool:
        return self.name_size > 0

    def compute_head_bytes(self) -> bytes:
        """
        Packs the 4-byte vm_obj_head_t:
        - u16 payload_size
        - u8  d: obj_t (4 bits) | (name_size (4 bits) << 4)
        - u8  f: flags bitfield
        """
        payload_sz = self.payload_size

        # d byte: low 4 bits = obj_t, high 4 bits = name_size
        d = (int(self.type) & 0x0F) | ((self.name_size & 0x0F) << 4)

        # f byte: flags
        f = 0
        if self.mutable:
            f |= VMFlags.MUTABLE
        # default to marked updated on creation so subscribers receive first frame
        f |= VMFlags.UPD
        if self.upd_resetable:
            f |= VMFlags.UPD_RESETABLE
        if self.is_tagged():
            f |= VMFlags.TAGGED
        if self.retentive:
            f |= VMFlags.RETENTIVE
        if self.usr_protected:
            f |= VMFlags.USR_PROTECTED

        return struct.pack("<HBB", payload_sz, d, f)

    def pack_initial_data(self) -> Optional[bytes]:
        """
        Packs initial_value into little-endian bytes for Packet 0x43 (Seed Data).
        Returns None if no initial value is defined.
        For VMType.PTR, packs the child object IDs (<H).
        """
        if self.type == VMType.PTR:
            child_list = self.children or (self.initial_value if isinstance(self.initial_value, (list, tuple)) else [self.initial_value])
            packed = bytearray()
            for child in child_list:
                if isinstance(child, VMObject):
                    if child.id is None:
                        raise ValueError(f"Child object {child} has not been assigned an ID before parent {self}!")
                    packed.extend(struct.pack("<H", child.id))
                else:
                    packed.extend(struct.pack("<H", int(child)))
            return bytes(packed)

        if self.initial_value is None:
            return None

        fmt = TYPE_STRUCT_FORMATS.get(self.type)
        if not fmt:
            if isinstance(self.initial_value, (bytes, bytearray)):
                return bytes(self.initial_value)
            return None

        values: List[Any]
        if isinstance(self.initial_value, (list, tuple)):
            values = list(self.initial_value)
        else:
            values = [self.initial_value]

        # Pad or truncate to self.count
        if len(values) < self.count:
            default_pad = 0 if self.type != VMType.F else 0.0
            values.extend([default_pad] * (self.count - len(values)))
        elif len(values) > self.count:
            values = values[:self.count]

        packed = bytearray()
        for v in values:
            if self.type == VMType.B:
                packed.extend(struct.pack(fmt, 1 if v else 0))
            elif self.type == VMType.F:
                packed.extend(struct.pack(fmt, float(v)))
            elif self.type in (VMType.U8, VMType.U32, VMType.I32, VMType.U64):
                packed.extend(struct.pack(fmt, int(v)))
            elif self.type == VMType.STR:
                if isinstance(v, str):
                    packed.extend(v.encode("ascii")[:1])
                else:
                    packed.extend(struct.pack("<B", int(v)))
            else:
                packed.extend(struct.pack(fmt, v))

        return bytes(packed)

    def field(self, name: str, elem_idx: int = 0) -> "Accessor":
        """
        Shortcut for accessing a scalar element of a tagged child inside a VM_OBJ_PTR container:
        Step 1: VM_IDX_NAME(name)
        Step 2: VM_IDX_LITERAL(elem_idx)
        """
        return self[name][elem_idx]

    def __getattr__(self, item: str) -> "Accessor":
        # Allow motor.speed or motor.current syntax
        for child in self.children:
            if child.name == item:
                return self[item]
        raise AttributeError(f"'{self}' has no child or attribute '{item}'")

    def __getitem__(self, index: Union[int, "Accessor", str]) -> "Accessor":
        """
        Relational indexing: returns an Accessor pointing to this object.
        - `obj[0]` -> literal index
        - `obj[other_acc]` -> dynamic index reference
        - `obj["field"]` -> name index
        """
        from .accessor_generator import Accessor, IndexStep
        return Accessor(self).at(index)

    def at(self, index: Union[int, "Accessor", str]) -> "Accessor":
        """Explicit indexing helper."""
        return self.__getitem__(index)

    def ref(self) -> "Accessor":
        """Returns an Accessor to index 0 (scalar shortcut)."""
        return self.__getitem__(0)

    def __repr__(self) -> str:
        id_str = f"id={self.id}" if self.id is not None else "id=UNASSIGNED"
        name_str = f" '{self.name}'" if self.name else " anonymous"
        return f"<VMObject {id_str}{name_str} type={self.type.name} items={self.count}>"
