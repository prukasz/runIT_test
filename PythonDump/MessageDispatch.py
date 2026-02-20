import struct
from enum import IntEnum
from typing import List, Optional, Callable
from dataclasses import dataclass

from Enums import mem_types_t, packet_header_t, mem_types_size, mem_types_pack_map, mem_types_map,  mem_types_to_str_map, emu_err_t, OWNER_NAMES, LOG_NAMES

class DisplayMode(IntEnum):
    PRETTY = 0   # nicely formatted, coloured output (default)
    RAW    = 1   # raw hex dump of the whole packet

_display_mode: DisplayMode = DisplayMode.PRETTY


def set_display_mode(mode: DisplayMode) -> None:
    global _display_mode
    _display_mode = DisplayMode(mode)


def get_display_mode() -> DisplayMode:
    return _display_mode

def _err_to_str(code: int) -> str:
    try:
        return emu_err_t(code).name.replace("EMU_ERR_", "")
    except ValueError:
        if code == 0:
            return "OK"
        return f"UNKNOWN(0x{code:04X})"


def _owner_to_str(owner_id: int) -> str:
    if 0 < owner_id < len(OWNER_NAMES):
        return OWNER_NAMES[owner_id]
    return f"OWNER({owner_id})"


def _log_to_str(log_id: int) -> str:
    if 0 <= log_id < len(LOG_NAMES):
        return LOG_NAMES[log_id]
    return f"LOG({log_id})"


# ═══════════════════════════════════════════════════════════════════
# ANSI color helpers
# ═══════════════════════════════════════════════════════════════════

class _C:
    """ANSI escape codes for colored terminal output."""
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    DIM     = "\033[2m"

    RED     = "\033[91m"
    GREEN   = "\033[92m"
    YELLOW  = "\033[93m"
    BLUE    = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN    = "\033[96m"
    WHITE   = "\033[97m"

    BG_RED    = "\033[41m"
    BG_YELLOW = "\033[43m"
    BG_GREEN  = "\033[42m"


# ═══════════════════════════════════════════════════════════════════
# PUBLISH parser (0xD0)
# ═══════════════════════════════════════════════════════════════════

@dataclass
class PublishEntry:
    """Single published variable from a PUBLISH packet."""
    inst_idx: int
    context: int
    mem_type: int
    updated: bool
    el_cnt: int          # element count (1 for scalars, >1 for arrays)
    values: list       # decoded values


def _parse_publish(payload: bytes) -> list[PublishEntry]:

    entries: list[PublishEntry] = []
    pos = 0

    while pos < len(payload):
        # Parse head: inst_idx (u16) + bitfield (u8) + pad (u8)  →  4 bytes
        inst_idx = struct.unpack_from(mem_types_pack_map[mem_types_t.MEM_U16], payload, pos)[0]
        pos += 2
        bitfield = payload[pos]
        pos += 1
        pos += 1  # skip struct padding byte

        context  = bitfield & 0x07           # bits [2:0]
        mem_type = (bitfield >> 3) & 0x0F    # bits [6:3]
        updated  = bool((bitfield >> 7) & 1) # bit  [7]

        # Parse el_cnt (u16 LE)
        el_cnt = struct.unpack_from(mem_types_pack_map[mem_types_t.MEM_U16], payload, pos)[0]
        pos += 2

        # Get type size
        try:
            t = mem_types_t(mem_type)
            type_size = mem_types_size[t]
            fmt = mem_types_pack_map[t]
        except (ValueError, KeyError):
            entries.append(PublishEntry(inst_idx, context, mem_type, updated, el_cnt, []))
            break

        values = []

        for i in range(el_cnt):
            val = struct.unpack_from(fmt, payload, pos)[0]
            pos += type_size
            values.append(val)

        entries.append(PublishEntry(inst_idx, context, mem_type, updated, el_cnt, values))

    return entries


def _format_publish(entries: list[PublishEntry]) -> str:
    """Format PUBLISH entries for human-readable display."""
    lines = []
    lines.append(f"{_C.CYAN}{_C.BOLD}╔══ PUBLISH ═══════════════════════════════════════╗{_C.RESET}")

    for i, e in enumerate(entries):
        type_name = mem_types_to_str_map.get(mem_types_t(e.mem_type), f"type({e.mem_type})") if e.mem_type in [t.value for t in mem_types_t] else f"type({e.mem_type})"
        upd_str = f"{_C.GREEN}● upd{_C.RESET}" if e.updated else f"{_C.DIM}○    {_C.RESET}"

        # Variable alias from registry
        alias = ""
        if _alias_registry and i < len(_alias_registry):
            alias = f' "{_C.YELLOW}{_alias_registry[i]}{_C.RESET}"'

        header = f"  {upd_str} ctx={e.context} idx={e.inst_idx:<4} {_C.BLUE}{type_name:>5}{_C.RESET}{alias}"

        if len(e.values) == 0:
            lines.append(f"{header}  → {_C.DIM}(no data){_C.RESET}")
        elif len(e.values) == 1:
            val = e.values[0]
            if isinstance(val, float):
                lines.append(f"{header}  → {_C.WHITE}{_C.BOLD}{val:>12.4f}{_C.RESET}")
            elif isinstance(val, bool):
                v = f"{_C.GREEN}TRUE" if val else f"{_C.RED}FALSE"
                lines.append(f"{header}  → {v}{_C.RESET}")
            else:
                lines.append(f"{header}  → {_C.WHITE}{_C.BOLD}{val}{_C.RESET}")
        else:
            # Array — show compact
            if len(e.values) <= 10:
                if isinstance(e.values[0], float):
                    arr_str = ", ".join(f"{v:.3f}" for v in e.values)
                else:
                    arr_str = ", ".join(str(v) for v in e.values)
                lines.append(f"{header}  → [{_C.WHITE}{arr_str}{_C.RESET}]  ({len(e.values)} el)")
            else:
                # Show first 5 + last 3
                if isinstance(e.values[0], float):
                    head = ", ".join(f"{v:.3f}" for v in e.values[:5])
                    tail = ", ".join(f"{v:.3f}" for v in e.values[-3:])
                else:
                    head = ", ".join(str(v) for v in e.values[:5])
                    tail = ", ".join(str(v) for v in e.values[-3:])
                lines.append(f"{header}  → [{_C.WHITE}{head}, ... {tail}{_C.RESET}]  ({len(e.values)} el)")

    lines.append(f"{_C.CYAN}╚══════════════════════════════════════════════════╝{_C.RESET}")
    return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════
# ERROR_LOG parser (0xE0)
# ═══════════════════════════════════════════════════════════════════

# ESP32-C6 is RISC-V 32-bit (ILP32), uint64_t aligned to 8
# emu_result_t layout (no __packed):
#   code:      u32 (enum)        offset 0   (4 bytes)
#   owner:     u16               offset 4   (2 bytes)
#   owner_idx: u16               offset 6   (2 bytes)
#   flags:     u8 bitfield       offset 8   (1 byte)
#   pad:       7 bytes           offset 9   (align to 8 for u64)
#   time:      u64               offset 16  (8 bytes)
#   cycle:     u64               offset 24  (8 bytes)
# Total: 32 bytes

EMU_RESULT_SIZE = 32
EMU_RESULT_FMT = '<IHHBxxxxxxxQQ'  # I=code, H=owner, H=owner_idx, B=flags, xxxxxxx=pad, Q=time, Q=cycle


@dataclass
class ErrorLogEntry:
    code: int
    owner: int
    owner_idx: int
    abort: bool
    warning: bool
    notice: bool
    depth: int
    time_ms: int
    cycle: int


def _parse_error_log(payload: bytes) -> List[ErrorLogEntry]:
    """Parse ERROR_LOG payload (after 0xE0 header byte)."""
    entries = []
    pos = 0

    while pos + EMU_RESULT_SIZE <= len(payload):
        code, owner, owner_idx, flags, time_ms, cycle = struct.unpack_from(
            EMU_RESULT_FMT, payload, pos
        )
        pos += EMU_RESULT_SIZE

        entries.append(ErrorLogEntry(
            code=code,
            owner=owner,
            owner_idx=owner_idx,
            abort=bool(flags & 0x01),
            warning=bool(flags & 0x02),
            notice=bool(flags & 0x04),
            depth=(flags >> 3) & 0x1F,
            time_ms=time_ms,
            cycle=cycle,
        ))

    return entries


def _format_error_log(entries: List[ErrorLogEntry]) -> str:
    """Format error log entries for display."""
    lines = []
    lines.append(f"{_C.RED}{_C.BOLD}╔══ ERROR LOG ═════════════════════════════════════╗{_C.RESET}")

    for e in entries:
        # Severity badge
        if e.abort:
            badge = f"{_C.BG_RED}{_C.WHITE}{_C.BOLD} ABORT {_C.RESET}"
        elif e.warning:
            badge = f"{_C.BG_YELLOW}{_C.BOLD} WARN  {_C.RESET}"
        elif e.notice:
            badge = f"{_C.DIM} note  {_C.RESET}"
        else:
            badge = f"{_C.RED} ERROR {_C.RESET}"

        err_str = _err_to_str(e.code)
        owner_str = _owner_to_str(e.owner)

        lines.append(
            f"  {badge} {_C.RED}{err_str}{_C.RESET}"
            f"  ← {_C.YELLOW}{owner_str}{_C.RESET}"
            f"{'[' + str(e.owner_idx) + ']'}"
            f"  {_C.DIM}t={e.time_ms}ms cyc={e.cycle} d={e.depth}{_C.RESET}"
        )

    lines.append(f"{_C.RED}╚══════════════════════════════════════════════════╝{_C.RESET}")
    return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════
# STATUS_LOG parser (0xE1)
# ═══════════════════════════════════════════════════════════════════

# emu_report_t layout (no __packed), uint64_t aligned to 8:
#   log:       u32 (enum)        offset 0   (4 bytes)
#   owner:     u32 (enum)        offset 4   (4 bytes)  — emu_owner_t is an enum = u32
#   owner_idx: u16               offset 8   (2 bytes)
#   pad:       6 bytes           offset 10  (align to 8 for u64)
#   time:      u64               offset 16  (8 bytes)
#   cycle:     u64               offset 24  (8 bytes)
# Total: 32 bytes

EMU_REPORT_SIZE = 32
EMU_REPORT_FMT = '<IIHxxxxxxQQ'  # I=log, I=owner, H=owner_idx, xxxxxx=pad, Q=time, Q=cycle


@dataclass
class StatusLogEntry:
    log: int
    owner: int
    owner_idx: int
    time_ms: int
    cycle: int


def _parse_status_log(payload: bytes) -> List[StatusLogEntry]:
    """Parse STATUS_LOG payload (after 0xE1 header byte)."""
    entries = []
    pos = 0

    while pos + EMU_REPORT_SIZE <= len(payload):
        log, owner, owner_idx, time_ms, cycle = struct.unpack_from(
            EMU_REPORT_FMT, payload, pos
        )
        pos += EMU_REPORT_SIZE

        entries.append(StatusLogEntry(
            log=log,
            owner=owner,  # mask to u16 — upper bytes may be uninitialized
            owner_idx=owner_idx,
            time_ms=time_ms,
            cycle=cycle,
        ))

    return entries


def _format_status_log(entries: List[StatusLogEntry]) -> str:
    """Format status log entries for display."""
    lines = []
    lines.append(f"{_C.GREEN}{_C.BOLD}╔══ STATUS LOG ════════════════════════════════════╗{_C.RESET}")

    for e in entries:
        log_str = _log_to_str(e.log)
        owner_str = _owner_to_str(e.owner)

        lines.append(
            f"  {_C.GREEN}ℹ{_C.RESET} {_C.WHITE}{log_str}{_C.RESET}"
            f"  ← {_C.YELLOW}{owner_str}{_C.RESET}"
            f"{'[' + str(e.owner_idx) + ']'}"
            f"  {_C.DIM}t={e.time_ms}ms cyc={e.cycle}{_C.RESET}"
        )

    lines.append(f"{_C.GREEN}╚══════════════════════════════════════════════════╝{_C.RESET}")
    return "\n".join(lines)


_subscription_registry: Optional[List[int]] = None    # el_cnt per subscription entry
_alias_registry: Optional[List[str]] = None           # alias per subscription entry


# def register_subscriptions(sub_builder) -> None:
#     """
#     Register subscription info from a SubscriptionBuilder so the dispatcher
#     can correctly parse arrays and show aliases in PUBLISH packets.

#     Call this AFTER building subscriptions but BEFORE connecting to device.

#     Parameters
#     ----------
#     sub_builder : SubscriptionBuilder
#         The subscription builder used to generate subscription packets.
#     """
#     global _subscription_registry, _alias_registry

#     _subscription_registry = []
#     _alias_registry = []

#     for entry in sub_builder._entries:
#         # Look up instance to find el_cnt
#         ctx_id = entry.ctx_id
#         m_type = entry.mem_type
#         inst_idx = entry.inst_idx

#         # Try to find the instance in the Code's memory contexts
#         code = sub_builder.code
#         if ctx_id == 0:
#             ctx = code.user_ctx
#         elif ctx_id == 1:
#             ctx = code.blocks_ctx
#         else:
#             _subscription_registry.append(1)
#             _alias_registry.append(f"ctx{ctx_id}:idx{inst_idx}")
#             continue

#         # Find instance by index in context storage
#         found = False
#         for inst in ctx.storage[mem_types_t(int(m_type))]:
#             if inst.idx == inst_idx:
#                 el_cnt = 1
#                 for d in inst.dims:
#                     el_cnt *= d
#                 _subscription_registry.append(max(el_cnt, 1))
#                 _alias_registry.append(inst.alias or f"idx{inst_idx}")
#                 found = True
#                 break

#         if not found:
#             _subscription_registry.append(1)
#             _alias_registry.append(f"idx{inst_idx}")


# def register_subscriptions_manual(entries: list) -> None:
#     """
#     Manually register subscription entries for PUBLISH parsing.

#     Parameters
#     ----------
#     entries : list of dict
#         Each dict: {"alias": str, "el_cnt": int}
#         In the same order as subscriptions were added.

#     Example:
#         register_subscriptions_manual([
#             {"alias": "selector",  "el_cnt": 1},
#             {"alias": "enable",    "el_cnt": 1},
#             {"alias": "gains",     "el_cnt": 10},
#             {"alias": "ton.Q",     "el_cnt": 1},
#         ])
#     """
#     global _subscription_registry, _alias_registry
#     _subscription_registry = [e["el_cnt"] for e in entries]
#     _alias_registry = [e["alias"] for e in entries]


_user_callbacks: dict[int, List[Callable]] = {
    packet_header_t.PACKET_H_PUBLISH:    [],
    packet_header_t.PACKET_H_ERROR_LOG:  [],
    packet_header_t.PACKET_H_STATUS_LOG: [],
}


def on_publish(callback: Callable[[List[PublishEntry]], None]) -> None:
    """Register a callback for PUBLISH (0xD0) packets. Receives list of PublishEntry."""
    _user_callbacks[packet_header_t.PACKET_H_PUBLISH].append(callback)


def on_error(callback: Callable[[List[ErrorLogEntry]], None]) -> None:
    """Register a callback for ERROR_LOG (0xE0) packets. Receives list of ErrorLogEntry."""
    _user_callbacks[packet_header_t.PACKET_H_ERROR_LOG].append(callback)


def on_status(callback: Callable[[List[StatusLogEntry]], None]) -> None:
    """Register a callback for STATUS_LOG (0xE1) packets. Receives list of StatusLogEntry."""
    _user_callbacks[packet_header_t.PACKET_H_STATUS_LOG].append(callback)


def dispatch_message(data: bytearray, quiet: bool = False) -> None:
    if not data:
        return

    header = data[0]
    payload = bytes(data[1:])

    # ── RAW mode: just dump the whole packet as hex ──────────────
    if _display_mode == DisplayMode.RAW:
        hex_str = " ".join(f"{b:02X}" for b in data)
        _HEADER_TAG = {
            packet_header_t.PACKET_H_PUBLISH:    "PUB",
            packet_header_t.PACKET_H_ERROR_LOG:  "ERR",
            packet_header_t.PACKET_H_STATUS_LOG: "STS",
        }
        tag = _HEADER_TAG.get(header, f"0x{header:02X}")
        if not quiet:
            print(f"[{tag}] ({len(data):>3}B) {hex_str}")
        # still fire callbacks with parsed data
        if header == packet_header_t.PACKET_H_PUBLISH:
            entries = _parse_publish(payload)
            for cb in _user_callbacks[packet_header_t.PACKET_H_PUBLISH]:
                cb(entries)
        elif header == packet_header_t.PACKET_H_ERROR_LOG:
            entries = _parse_error_log(payload)
            for cb in _user_callbacks[packet_header_t.PACKET_H_ERROR_LOG]:
                cb(entries)
        elif header == packet_header_t.PACKET_H_STATUS_LOG:
            entries = _parse_status_log(payload)
            for cb in _user_callbacks[packet_header_t.PACKET_H_STATUS_LOG]:
                cb(entries)
        return

    # ── PRETTY mode (default) ───────────────────────────────────
    if header == packet_header_t.PACKET_H_PUBLISH:
        entries = _parse_publish(payload)
        if not quiet:
            print(_format_publish(entries))
        for cb in _user_callbacks[packet_header_t.PACKET_H_PUBLISH]:
            cb(entries)

    elif header == packet_header_t.PACKET_H_ERROR_LOG:
        entries = _parse_error_log(payload)
        if not quiet:
            print(_format_error_log(entries))
        for cb in _user_callbacks[packet_header_t.PACKET_H_ERROR_LOG]:
            cb(entries)

    elif header == packet_header_t.PACKET_H_STATUS_LOG:
        entries = _parse_status_log(payload)
        if not quiet:
            print(_format_status_log(entries))
        for cb in _user_callbacks[packet_header_t.PACKET_H_STATUS_LOG]:
            cb(entries)

def notification_handler(sender, data: bytearray) -> None:

    dispatch_message(data)
