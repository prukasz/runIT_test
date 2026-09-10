"""
High-level VM Program container, relational builder, and packet compiler.
"""
from __future__ import annotations
import asyncio
from dataclasses import dataclass
from typing import Any, List, Optional, Sequence, Tuple, Union

from .vm_types import (
    VMType,
    VMFlags,
    VMExecCommand,
)
from .object import VMObject
from .accessor_generator import Accessor, AccessorGenerator
from .packets import (
    encode_vm_reset,
    encode_vm_open,
    encode_vm_add_objs,
    encode_vm_set_data,
    encode_vm_subscribe,
    encode_vm_exec,
)


@dataclass
class CompiledPacket:
    """A compiled wire packet ready for transmission."""
    name: str
    data: bytes

    @property
    def hex(self) -> str:
        return " ".join(f"{b:02X}" for b in self.data)

    def __len__(self) -> int:
        return len(self.data)


class VMCompiledProgram:
    """Holds compiled wire packets and hardware transmission helpers."""

    def __init__(
        self,
        packets: List[CompiledPacket],
        objects: List[VMObject],
        accessors: List[Accessor],
        total_arena_size: int,
    ):
        self.packets = packets
        self.objects = objects
        self.accessors = accessors
        self.total_arena_size = total_arena_size

    def dump_hex(self, max_hex_per_line: int = 16) -> str:
        """Returns a human-readable table of all wire packets."""
        lines = []
        lines.append(f"=== VM Compiled Program ({len(self.packets)} packets, {self.total_arena_size} bytes arena) ===")
        lines.append(f"Objects: {len(self.objects)} | Accessors: {len(self.accessors)}")
        lines.append("-" * 75)
        for i, pkt in enumerate(self.packets, 1):
            hex_bytes = [f"{b:02X}" for b in pkt.data]
            if len(hex_bytes) <= max_hex_per_line:
                lines.append(f"{i:2d}. {pkt.name:<36} [{len(pkt):2d} B] -> {' '.join(hex_bytes)}")
            else:
                chunk0 = " ".join(hex_bytes[:max_hex_per_line])
                lines.append(f"{i:2d}. {pkt.name:<36} [{len(pkt):2d} B] -> {chunk0}")
                for off in range(max_hex_per_line, len(hex_bytes), max_hex_per_line):
                    chunk = " ".join(hex_bytes[off : off + max_hex_per_line])
                    lines.append(f"{' ':43}       {chunk}")
        lines.append("=" * 75)
        return "\n".join(lines)

    async def send_to_ble(
        self,
        ble_client: Any,
        char_target: Union[str, int] = "0xFFE1",
        delay_ms: int = 40,
        start_exec: bool = True,
    ):
        """
        Transmits all packets sequentially to a runIT board via RunITBLEClient.
        """
        for pkt in self.packets:
            if not start_exec and pkt.data[1] == 0x48:
                continue
            await ble_client.send_bytes(char_target, pkt.data)
            await asyncio.sleep(delay_ms / 1000.0)


class VMProgram:
    """
    Builder for declaring VM Objects, Accessors, and compiling wire packets.

    Rules:
    - Object and Accessor IDs are NOT specified by the user.
    - They are defined relationally, and integer IDs are assigned automatically
      at compile time.
    - Names are optional: named user variables get tag names; intermediate
      anonymous variables have name=None.
    - Serves as the foundation for connecting future execution blocks.
    """

    def __init__(self, name: str = "runit_program"):
        self.name = name
        self.objects: List[VMObject] = []
        self.accessor_gen = AccessorGenerator()
        self.subscribed_objects: List[VMObject] = []
        self.blocks: List[Any] = []  # Reserved for upcoming execution block builders

    def add_var(
        self,
        name: str,
        type: VMType,
        count: int = 1,
        initial: Optional[Any] = None,
        mutable: bool = True,
        retentive: bool = False,
        usr_protected: bool = False,
        upd_resetable: Optional[bool] = None,
    ) -> VMObject:
        """Adds a named user variable to the program."""
        obj = VMObject(
            type=type,
            count=count,
            name=name,
            initial=initial,
            mutable=mutable,
            retentive=retentive,
            usr_protected=usr_protected,
            upd_resetable=upd_resetable,
        )
        self.objects.append(obj)
        return obj

    def add_const(
        self,
        name: str,
        type: VMType,
        value: Any,
        count: int = 1,
    ) -> VMObject:
        """
        Adds an immutable constant literal (mutable=False, upd_resetable=False).
        Permanently fresh (standing input for calculation blocks).
        """
        return self.add_var(
            name=name,
            type=type,
            count=count,
            initial=value,
            mutable=False,
            upd_resetable=False,
        )

    def add_temp(
        self,
        type: VMType,
        count: int = 1,
        initial: Optional[Any] = None,
        mutable: bool = True,
        upd_resetable: Optional[bool] = None,
    ) -> VMObject:
        """Adds an anonymous / intermediate variable (no name, saves bytes)."""
        obj = VMObject(
            type=type,
            count=count,
            name=None,
            initial=initial,
            mutable=mutable,
            upd_resetable=upd_resetable,
        )
        self.objects.append(obj)
        return obj

    def add_container(
        self,
        name: Optional[str],
        children: Sequence[VMObject],
        mutable: bool = True,
        upd_resetable: Optional[bool] = None,
    ) -> VMObject:
        """
        Adds a VM_OBJ_PTR parent container linking to children.
        Ensures children are registered in the program prior to the container.
        """
        for child in children:
            if child not in self.objects:
                self.objects.append(child)

        container = VMObject(
            type=VMType.PTR,
            count=len(children),
            name=name,
            children=list(children),
            mutable=mutable,
            upd_resetable=upd_resetable,
        )
        self.objects.append(container)
        return container

    def add_struct(
        self,
        name: str,
        fields: Dict[str, Any],
        upd_resetable: Optional[bool] = None,
    ) -> VMObject:
        """
        High-level struct helper:
        Creates tagged child objects for each field, then binds them under a VM_OBJ_PTR container.

        Example:
            motor = prog.add_struct("Motor", {
                "speed": (VMType.F, 0.0),
                "current": (VMType.F, 1.2),
                "enabled": (VMType.B, True),
            })
            # Accessing fields:
            acc_speed = prog.add_accessor(motor.speed[0])
            acc_cur   = prog.add_accessor(motor["current"][0])
        """
        child_objs: List[VMObject] = []
        for fname, fdesc in fields.items():
            if isinstance(fdesc, (VMType, int)):
                ftype = VMType(fdesc)
                fcount = 1
                finit = None
            elif isinstance(fdesc, (list, tuple)):
                if len(fdesc) == 2:
                    ftype, finit = fdesc
                    fcount = len(finit) if isinstance(finit, (list, tuple)) else 1
                elif len(fdesc) >= 3:
                    ftype, fcount, finit = fdesc[0], fdesc[1], fdesc[2]
                else:
                    ftype, fcount, finit = fdesc[0], 1, None
            else:
                raise ValueError(f"Invalid field descriptor for '{fname}': {fdesc}")

            child = self.add_var(name=fname, type=ftype, count=fcount, initial=finit, upd_resetable=upd_resetable)
            child_objs.append(child)

        return self.add_container(name=name, children=child_objs, upd_resetable=upd_resetable)

    def add_object(self, obj: VMObject) -> VMObject:
        """Registers a pre-constructed VMObject."""
        if obj not in self.objects:
            self.objects.append(obj)
        return obj

    def add_accessor(self, accessor: Accessor) -> Accessor:
        """Explicitly registers an accessor in the generator."""
        return self.accessor_gen.register(accessor)

    def subscribe(self, *objects: VMObject):
        """Marks one or more objects for live BLE telemetry streaming (Packet 0x47)."""
        for obj in objects:
            if obj not in self.subscribed_objects:
                self.subscribed_objects.append(obj)

    def compile(
        self,
        arena_size: Optional[int] = None,
        include_exec: bool = True,
        exec_cmd: VMExecCommand = VMExecCommand.NORMAL_MODE,
    ) -> VMCompiledProgram:
        """
        Compiles all declared objects, accessors, and blocks into wire packets.

        1. Assigns automatic sequential IDs to VMObjects (0..N-1).
        2. Resolves dependencies and assigns automatic IDs to Accessors (0..M-1).
        3. Estimates memory arena footprint if arena_size is not provided.
        4. Serializes binary packets (0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48).
        """
        # Step 1: Telemetry Safety & Automatic Object IDs
        # Subscribed objects (and any children in their trees) MUST be upd_resetable=True
        # unless the user explicitly forced upd_resetable=False.
        def _ensure_telemetry_resettable(o: VMObject):
            if not getattr(o, "_explicit_upd_resetable", False):
                o.upd_resetable = True
            for c in o.children:
                _ensure_telemetry_resettable(c)

        for sub_obj in self.subscribed_objects:
            _ensure_telemetry_resettable(sub_obj)

        for idx, obj in enumerate(self.objects):
            obj.id = idx

        # Step 2: Compile and assign Accessor IDs via AccessorGenerator
        accessors = self.accessor_gen.compile()

        # Step 3: Compute arena memory requirements (aligned to 4 bytes)
        def align4(n: int) -> int:
            return (n + 3) & ~3

        obj_cnt = len(self.objects)
        acc_cnt = len(accessors)
        blk_cnt = len(self.blocks)

        registry_bytes = (obj_cnt + acc_cnt + blk_cnt) * 4

        # Object footprint: sizeof(vm_obj_head_t)=4 + payload_size + name_size
        obj_bytes = 0
        for obj in self.objects:
            obj_bytes += align4(4 + obj.payload_size + obj.name_size)

        # Accessor footprint: sizeof(vm_accessor_t)=20 + indices trailing array
        acc_bytes = 0
        for acc in accessors:
            acc_bytes += align4(20 + len(acc.steps) * 8)

        # Base calculated required size plus safety headroom (at least 2048 bytes default)
        calc_total = registry_bytes + obj_bytes + acc_bytes + 512
        final_arena_size = arena_size if arena_size is not None else max(2048, align4(calc_total))

        # Step 4: Generate wire packets in exact sequence
        compiled_packets: List[CompiledPacket] = []

        # 1. Reset (0x40)
        compiled_packets.append(CompiledPacket(
            name="1. VM Reset (0x40)",
            data=encode_vm_reset(),
        ))

        # 2. Open Container (0x41)
        compiled_packets.append(CompiledPacket(
            name="2. VM Open Container (0x41)",
            data=encode_vm_open(obj_cnt, acc_cnt, blk_cnt, final_arena_size),
        ))

        # 3. Add Objects Batch (0x42)
        obj_records = [
            (obj.id, obj.compute_head_bytes(), obj.name_bytes if obj.is_tagged() else None)
            for obj in self.objects
        ]
        add_obj_pkts = encode_vm_add_objs(obj_records)
        for i, p in enumerate(add_obj_pkts):
            label = "3. Add Objects Batch (0x42)" if len(add_obj_pkts) == 1 else f"3. Add Objects Batch part {i+1} (0x42)"
            compiled_packets.append(CompiledPacket(name=label, data=p))

        # 4. Seed Initial Data (0x43) - if any object defines initial data
        seed_records = []
        for obj in self.objects:
            data = obj.pack_initial_data()
            if data is not None and len(data) > 0:
                seed_records.append((obj.id, 0, data))

        if seed_records:
            seed_pkts = encode_vm_set_data(seed_records)
            for i, p in enumerate(seed_pkts):
                label = "4. Seed Initial Data (0x43)" if len(seed_pkts) == 1 else f"4. Seed Initial Data part {i+1} (0x43)"
                compiled_packets.append(CompiledPacket(name=label, data=p))

        # 5. Add Accessors Batch (0x44)
        acc_pkts = self.accessor_gen.generate_packets()
        for i, p in enumerate(acc_pkts):
            label = "5. Add Accessors Batch (0x44)" if len(acc_pkts) == 1 else f"5. Add Accessors Batch part {i+1} (0x44)"
            compiled_packets.append(CompiledPacket(name=label, data=p))

        # 6. Add Blocks (0x45) - placeholder for upcoming block tests

        # 7. Subscribe Live Telemetry (0x47) - if any subscribed objects
        if self.subscribed_objects:
            sub_ids = [o.id for o in self.subscribed_objects if o.id is not None]
            if sub_ids:
                compiled_packets.append(CompiledPacket(
                    name="9. Subscribe Live Telemetry (0x47)",
                    data=encode_vm_subscribe(sub_ids),
                ))

        # 8. Execution Start (0x48)
        if include_exec:
            compiled_packets.append(CompiledPacket(
                name=f"10. Execution Start (0x48 {exec_cmd.name})",
                data=encode_vm_exec(exec_cmd),
            ))

        return VMCompiledProgram(
            packets=compiled_packets,
            objects=self.objects,
            accessors=accessors,
            total_arena_size=final_arena_size,
        )
