"""
VM Type definitions, header constants, flag bitfields, and wire codecs.
Matches components/VM/core/obj/vm_obj.h and components/codecs/decoders/dec_vm_loader.h.
Named vm_types.py to avoid shadowing the Python standard library `types` module.
"""
from enum import IntEnum
from typing import Tuple, Dict

# =============================================================================
# Protocol Header Constants (Class 0x04: VM Program Loader)
# =============================================================================
CLASS_VM_LOADER = 0x04

HEADER_PKT_VM_RESET      = 0x40
HEADER_PKT_VM_OPEN       = 0x41
HEADER_PKT_VM_ADD_OBJS   = 0x42
HEADER_PKT_VM_SET_DATA   = 0x43
HEADER_PKT_VM_ADD_ACC    = 0x44
HEADER_PKT_VM_ADD_BLOCK  = 0x45
HEADER_PKT_VM_SUBSCRIBE  = 0x47
HEADER_PKT_VM_EXEC       = 0x48

# Maximum length for an object's tag name in bytes (fits in 4-bit name_size field)
VM_OBJ_NAME_MAX = 15


class VMType(IntEnum):
    """VM data types (vm_obj_t_e)."""
    NONE = 0
    PTR  = 1  # 4 bytes (pointers / child object links)
    U8   = 2  # 1 byte  (unsigned 8-bit integer)
    U32  = 3  # 4 bytes (unsigned 32-bit integer)
    I32  = 4  # 4 bytes (signed 32-bit integer)
    F    = 5  # 4 bytes (IEEE 754 float32)
    B    = 6  # 1 byte  (boolean: 0 or 1)
    STR  = 7  # 1 byte  (character / string element)
    U64  = 8  # 8 bytes (unsigned 64-bit integer)


# Type widths in bytes
TYPE_SIZES: Dict[VMType, int] = {
    VMType.NONE: 0,
    VMType.PTR:  4,
    VMType.U8:   1,
    VMType.U32:  4,
    VMType.I32:  4,
    VMType.F:    4,
    VMType.B:    1,
    VMType.STR:  1,
    VMType.U64:  8,
}

# Struct format strings for single scalar items (little-endian)
TYPE_STRUCT_FORMATS: Dict[VMType, str] = {
    VMType.NONE: "",
    VMType.PTR:  "<I",
    VMType.U8:   "<B",
    VMType.U32:  "<I",
    VMType.I32:  "<i",
    VMType.F:    "<f",
    VMType.B:    "<B",
    VMType.STR:  "<B",
    VMType.U64:  "<Q",
}


class VMFlags:
    """vm_obj_head_t flags bitfield constants."""
    MUTABLE       = 0x01  # Bit 0: Is value editable
    UPD           = 0x02  # Bit 1: Updated / refreshed lately
    UPD_RESETABLE = 0x04  # Bit 2: Can update flag be reset
    TAGGED        = 0x08  # Bit 3: Is name field populated (set automatically when name_size > 0)
    RETENTIVE     = 0x10  # Bit 4: Stored in NVS across power cycles
    DYNAMIC       = 0x20  # Bit 5: Heap allocation
    USR_PROTECTED = 0x40  # Bit 6: User writes denied


class VMIndexKind(IntEnum):
    """Accessor indexing kind (vm_index_kind_e)."""
    LITERAL = 0  # Fixed integer position, resolved at compile time (u32, 5B)
    REF     = 1  # Resolves another accessor and reads it as index (u16, 3B)
    NAME    = 2  # Matches a child object's tag name (u8 len + ASCII bytes)


class VMExecCommand(IntEnum):
    """VM runtime execution commands (Packet 0x48)."""
    SCAN_MODE      = 0x00  # VM_EXEC_SCAN_MODE
    START          = 0x01  # VM_EXEC_START / ONCE
    STOP           = 0x02  # VM_EXEC_STOP (Stops cyclic passes)
    STEP           = 0x03  # VM_EXEC_STEP / NEXT
    RESET_TO_START = 0x04  # VM_EXEC_RESET_TO_START
    NORMAL_MODE    = 0x05  # VM_EXEC_NORMAL_MODE (Continuous cyclic run)
    PAUSE          = 0x06  # VM_EXEC_PAUSE
    RESUME         = 0x07  # VM_EXEC_RESUME
    RESET          = 0x08  # VM_EXEC_RESET (Full loader reset)

