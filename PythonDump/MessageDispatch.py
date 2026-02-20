"""
Message Dispatcher — parses and pretty-prints BLE notifications from the emulator.

Handles three packet types sent from the device:
    0xD0 — PUBLISH    (subscription data updates)
    0xE0 — ERROR_LOG  (error reports)
    0xE1 — STATUS_LOG (status / info reports)

Usage:
    from MessageDispatch import dispatch_message, set_display_mode, DisplayMode

    set_display_mode(DisplayMode.RAW)   # show raw hex packets
    set_display_mode(DisplayMode.PRETTY) # show formatted output (default)

    def notification_handler(sender, data: bytearray):
        dispatch_message(data)
"""

import struct
from enum import IntEnum
from typing import List, Optional, Callable
from dataclasses import dataclass

from Enums import mem_types_t, mem_types_pack_map, mem_types_size


# ═══════════════════════════════════════════════════════════════════
# Display mode
# ═══════════════════════════════════════════════════════════════════

class DisplayMode(IntEnum):
    """Controls how dispatch_message renders output."""
    PRETTY = 0   # nicely formatted, coloured output (default)
    RAW    = 1   # raw hex dump of the whole packet

_display_mode: DisplayMode = DisplayMode.PRETTY


def set_display_mode(mode: DisplayMode) -> None:
    """Switch between PRETTY and RAW output for dispatch_message."""
    global _display_mode
    _display_mode = DisplayMode(mode)


def get_display_mode() -> DisplayMode:
    """Return the current display mode."""
    return _display_mode


# ═══════════════════════════════════════════════════════════════════
# Constants
# ═══════════════════════════════════════════════════════════════════

HEADER_PUBLISH    = 0xD0
HEADER_ERROR_LOG  = 0xE1
HEADER_STATUS_LOG = 0xE0

# Type sizes matching C mem_types.h (indexed by mem_types_t value)
_TYPE_SIZE = {
    mem_types_t.MEM_U8:  1,
    mem_types_t.MEM_U16: 2,
    mem_types_t.MEM_U32: 4,
    mem_types_t.MEM_I16: 2,
    mem_types_t.MEM_I32: 4,
    mem_types_t.MEM_B:   1,
    mem_types_t.MEM_F:   4,
}

_TYPE_FMT = {
    mem_types_t.MEM_U8:  '<B',
    mem_types_t.MEM_U16: '<H',
    mem_types_t.MEM_U32: '<I',
    mem_types_t.MEM_I16: '<h',
    mem_types_t.MEM_I32: '<i',
    mem_types_t.MEM_B:   '<?',
    mem_types_t.MEM_F:   '<f',
}

_TYPE_NAME = {
    mem_types_t.MEM_U8:  "u8",
    mem_types_t.MEM_U16: "u16",
    mem_types_t.MEM_U32: "u32",
    mem_types_t.MEM_I16: "i16",
    mem_types_t.MEM_I32: "i32",
    mem_types_t.MEM_B:   "bool",
    mem_types_t.MEM_F:   "float",
}


# ═══════════════════════════════════════════════════════════════════
# Error / Owner / Log enums — mirrors C error_types.h
# ═══════════════════════════════════════════════════════════════════

class emu_err_t(IntEnum):
    EMU_OK                          = 0x0000
    EMU_ERR_INVALID_STATE           = 0xE001
    EMU_ERR_INVALID_ARG             = 0xE002
    EMU_ERR_INVALID_DATA            = 0xE003
    EMU_ERR_PACKET_EMPTY            = 0xE004
    EMU_ERR_PACKET_INCOMPLETE       = 0xE005
    EMU_ERR_PACKET_NOT_FOUND        = 0xE006
    EMU_ERR_PARSE_INVALID_REQUEST   = 0xE007
    EMU_ERR_DENY                    = 0xE008
    EMU_ERR_ORD_FAILED              = 0xE009
    EMU_ERR_ORD_DENY                = 0xE00A
    EMU_ERR_ORD_CANNOT_EXECUTE      = 0xE00B
    EMU_ERR_UNLIKELY                = 0xEFFF
    EMU_ERR_NO_MEM                  = 0xF000
    EMU_ERR_MEM_ALLOC               = 0xF001
    EMU_ERR_MEM_ACCESS_ALLOC        = 0xF002
    EMU_ERR_MEM_INVALID_REF_ID      = 0xF003
    EMU_ERR_MEM_INVALID_IDX         = 0xF004
    EMU_ERR_MEM_OUT_OF_BOUNDS       = 0xF005
    EMU_ERR_MEM_INVALID_DATATYPE    = 0xF006
    EMU_ERR_NULL_PTR                = 0xF007
    EMU_ERR_NULL_PTR_ACCESS         = 0xF008
    EMU_ERR_NULL_PTR_INSTANCE       = 0xF009
    EMU_ERR_NULL_PTR_CONTEXT        = 0xF00A
    EMU_ERR_MEM_ALREADY_CREATED     = 0xF00B
    EMU_ERR_BLOCK_Generic           = 0xB000
    EMU_ERR_BLOCK_DIV_BY_ZERO       = 0xB001
    EMU_ERR_BLOCK_OUT_OF_RANGE      = 0xB002
    EMU_ERR_BLOCK_INVALID_PARAM     = 0xB003
    EMU_ERR_BLOCK_COMPUTE_IDX       = 0xB004
    EMU_ERR_BLOCK_FOR_TIMEOUT       = 0xB005
    EMU_ERR_BLOCK_INVALID_CONN      = 0xB006
    EMU_ERR_BLOCK_ALREADY_FILLED    = 0xB007
    EMU_ERR_BLOCK_WTD_TRIGGERED     = 0xB008
    EMU_ERR_BLOCK_USE_INTERNAL_VAR  = 0xB009
    EMU_ERR_BLOCK_INACTIVE          = 0xB00A
    EMU_ERR_BLOCK_FAILED            = 0xB00B
    EMU_ERR_WTD_TRIGGERED           = 0xA000


# Sequential enums — values assigned automatically after 0xA000
# We mirror the C enum ordering from error_types.h
_EMU_ERR_EXTRA_START = 0xA001
_EMU_ERR_EXTRA = {
    _EMU_ERR_EXTRA_START + 0: "MEM_INVALID_ACCESS",
    _EMU_ERR_EXTRA_START + 1: "LOOP_NOT_INITIALIZED",
    _EMU_ERR_EXTRA_START + 2: "BLOCK_SELECTOR_OOB",
    _EMU_ERR_EXTRA_START + 3: "CTX_INVALID_ID",
    _EMU_ERR_EXTRA_START + 4: "CTX_ALREADY_CREATED",
    _EMU_ERR_EXTRA_START + 5: "INVALID_PACKET_SIZE",
    _EMU_ERR_EXTRA_START + 6: "SEQUENCE_VIOLATION",
    _EMU_ERR_EXTRA_START + 7: "SUBSCRIPTION_FULL",
}


def _err_to_str(code: int) -> str:
    """Convert error code to human-readable string."""
    try:
        return emu_err_t(code).name.replace("EMU_ERR_", "")
    except ValueError:
        name = _EMU_ERR_EXTRA.get(code)
        if name:
            return name
        if code == 0:
            return "OK"
        return f"UNKNOWN(0x{code:04X})"


# Owner enum — sequential starting from 1, mirrors C error_types.h
_OWNER_NAMES = [
    "",  # 0 unused
    "mem_free_contexts",
    "mem_alloc_context",
    "mem_parse_create_context",
    "mem_parse_create_scalar_instances",
    "mem_parse_create_array_instances",
    "mem_parse_scalar_data",
    "mem_parse_array_data",
    "mem_parse_context_data_packets",
    "mem_set",
    "access_system_init",
    "parse_manager",
    "parse_source_add",
    "parse_blocks_total_cnt",
    "parse_block",
    "parse_blocks_verify_all",
    "parse_block_inputs",
    "parse_block_outputs",
    "loop_init",
    "loop_start",
    "loop_stop",
    "loop_set_period",
    "loop_run_once",
    "loop_deinit",
    "execute_code",
    "if_execute_loop_start",
    "if_execute_loop_stop",
    "if_execute_loop_init",
    "block_timer",
    "block_timer_parse",
    "block_timer_verify",
    "block_set",
    "block_set_parse",
    "block_set_verify",
    "block_math_parse",
    "block_math",
    "block_math_verify",
    "block_for",
    "block_for_parse",
    "block_for_verify",
    "block_logic_parse",
    "block_logic",
    "block_logic_verify",
    "block_counter",
    "block_counter_parse",
    "block_counter_verify",
    "block_clock",
    "block_clock_parse",
    "block_clock_verify",
    "block_latch",
    "block_latch_parse",
    "block_latch_verify",
    "block_set_output",
    "mem_register_context",
    "_parse_scalar_data",
    "_parse_array_data",
    "mem_pool_access_scalar_create",
    "access_system_free",
    "mem_access_parse_node_recursive",
    "_resolve_mem_offset",
    "mem_get",
    "block_check_in_true",
    "block_in_selector",
    "block_q_selector",
    "mem_context_delete",
    "mem_allocate_context",
    "mem_access_allocate_space",
    "mem_parse_instance_packet",
    "mem_fill_instance_scalar",
    "mem_fill_instance_array",
    "mem_parse_access_create",
    "parse_cfg",
    "block_parse_input",
    "block_parse_output",
    "parse_block_data",
    "subscribe_parse_init",
    "subscribe_parse_register",
    "subscribe_process",
    "subscribe_reset",
    "subscribe_send",
]


def _owner_to_str(owner_id: int) -> str:
    if 0 < owner_id < len(_OWNER_NAMES):
        return _OWNER_NAMES[owner_id]
    return f"OWNER({owner_id})"


# Log enum — sequential, mirrors C error_types.h emu_log_t
_LOG_NAMES = [
    "context_freed",
    "context_allocated",
    "scalars_created",
    "arrays_created",
    "data_parsed",
    "var_set",
    "access_sys_initialized",
    "loop_initialized",
    "loop_started",
    "loop_stopped",
    "period_changed",
    "loop_single_step",
    "interface_loop_init",
    "source_added",
    "execution_finished",
    "blocks_list_allocated",
    "blocks_parsed_partial",
    "blocks_parsed_all",
    "blocks_verified",
    "block_timer_executed",
    "block_timer_parsed",
    "block_timer_verified",
    "block_set_executed",
    "block_math_parsed",
    "block_math_executed",
    "block_math_verified",
    "block_for_executed",
    "block_for_parsed",
    "block_for_verified",
    "block_logic_parsed",
    "block_logic_executed",
    "block_logic_verified",
    "block_counter_idle",
    "block_counter_executed",
    "block_counter_parsed",
    "block_counter_verified",
    "block_clock_idle",
    "block_clock_executed",
    "block_clock_parsed",
    "block_clock_verified",
    "context_registered",
    "access_pool_allocated",
    "mem_set",
    "mem_access_parse_success",
    "loop_starting",
    "variables_allocated",
    "loop_ran_once",
    "loop_period_set",
    "resolving_access",
    "access_out_of_bounds",
    "mem_invalid_data_type",
    "mem_get_failed",
    "executing_block",
    "loop_reinitialized",
    "loop_task_already_exists",
    "blocks_parsed_once",
    "parsed_block_inputs",
    "parsed_block_outputs",
    "block_inactive",
    "finished",
    "block_selector_executed",
    "block_selector_verified",
    "block_selector_freed",
    "block_selector_parsed",
    "CTX_DESTROYED",
    "ctx_created",
    "created_ctx",
    "clock_out_active",
    "clock_out_inactive",
    "to_large_to_sub",
]


def _log_to_str(log_id: int) -> str:
    if 0 <= log_id < len(_LOG_NAMES):
        return _LOG_NAMES[log_id]
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
    values: list         # decoded values


def _parse_publish(payload: bytes) -> List[PublishEntry]:
    """
    Parse PUBLISH payload (after 0xD0 header byte).

    Each entry in the packet (matches C pub_instance_t send layout):
        [inst_idx: u16 LE]                       — 2 bytes
        [context:3 | type:4 | updated:1]         — 1 byte bitfield
        [padding]                                — 1 byte  (struct alignment pad)
        [el_cnt:  u16 LE]                        — 2 bytes  (element count)
        [data:    el_cnt × sizeof(type)]          — N bytes

    Total head = 4 bytes (sizeof(head) in C includes 1-byte padding after
    the bitfield to align the struct size to a multiple of uint16_t alignment).
    """
    entries: List[PublishEntry] = []
    pos = 0

    while pos < len(payload):
        # Need at least 6 bytes: head(4) + el_cnt(2)
        if pos + 6 > len(payload):
            break

        # Parse head: inst_idx (u16) + bitfield (u8) + pad (u8)  →  4 bytes
        inst_idx = struct.unpack_from('<H', payload, pos)[0]
        pos += 2
        bitfield = payload[pos]
        pos += 1
        pos += 1  # skip struct padding byte

        context  = bitfield & 0x07           # bits [2:0]
        mem_type = (bitfield >> 3) & 0x0F    # bits [6:3]
        updated  = bool((bitfield >> 7) & 1) # bit  [7]

        # Parse el_cnt (u16 LE)
        el_cnt = struct.unpack_from('<H', payload, pos)[0]
        pos += 2

        # Get type size
        try:
            t = mem_types_t(mem_type)
            type_size = _TYPE_SIZE[t]
            fmt = _TYPE_FMT[t]
        except (ValueError, KeyError):
            # Unknown type — can't continue parsing reliably
            entries.append(PublishEntry(inst_idx, context, mem_type, updated, el_cnt, []))
            break

        data_size = el_cnt * type_size
        remaining = len(payload) - pos
        if data_size > remaining:
            # Truncated packet — read what we can
            el_cnt = remaining // type_size
            data_size = el_cnt * type_size

        values = []
        for i in range(el_cnt):
            val = struct.unpack_from(fmt, payload, pos)[0]
            pos += type_size
            values.append(val)

        entries.append(PublishEntry(inst_idx, context, mem_type, updated, el_cnt, values))

    return entries


def _format_publish(entries: List[PublishEntry]) -> str:
    """Format PUBLISH entries for human-readable display."""
    lines = []
    lines.append(f"{_C.CYAN}{_C.BOLD}╔══ PUBLISH ═══════════════════════════════════════╗{_C.RESET}")

    for i, e in enumerate(entries):
        type_name = _TYPE_NAME.get(mem_types_t(e.mem_type), f"type({e.mem_type})") if e.mem_type in [t.value for t in mem_types_t] else f"type({e.mem_type})"
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
            f"{'[' + str(e.owner_idx) + ']' if e.owner_idx != 0xFFFF else ''}"
            f"  {_C.DIM}t={e.time_ms}ms cyc={e.cycle} d={e.depth}{_C.RESET}"
        )

    lines.append(f"{_C.RED}╚══════════════════════════════════════════════════╝{_C.RESET}")
    return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════
# STATUS_LOG parser (0xE1)
# ═══════════════════════════════════════════════════════════════════

# emu_report_t layout (no __packed), uint64_t aligned to 8:
#   log:       u32 (enum)        offset 0   (4 bytes)
#   owner:     u16               offset 4   (2 bytes)  — emu_owner_t on wire as u16
#   owner_idx: u16               offset 6   (2 bytes)
#   pad:       8 bytes           offset 8   (align to 8 for u64)
#   time:      u64               offset 16  (8 bytes)
#   cycle:     u64               offset 24  (8 bytes)
# Total: 32 bytes

EMU_REPORT_SIZE = 32
EMU_REPORT_FMT = '<IHHxxxxxxxxQQ'  # I=log, H=owner, H=owner_idx, xxxxxxxx=pad, Q=time, Q=cycle


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
            owner=owner & 0xFFFF,  # mask to u16 — upper bytes may be uninitialized
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
            f"{'[' + str(e.owner_idx) + ']' if e.owner_idx != 0xFFFF else ''}"
            f"  {_C.DIM}t={e.time_ms}ms cyc={e.cycle}{_C.RESET}"
        )

    lines.append(f"{_C.GREEN}╚══════════════════════════════════════════════════╝{_C.RESET}")
    return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════
# Subscription registry — for correct PUBLISH parsing of arrays
# ═══════════════════════════════════════════════════════════════════

_subscription_registry: Optional[List[int]] = None    # el_cnt per subscription entry
_alias_registry: Optional[List[str]] = None           # alias per subscription entry


def register_subscriptions(sub_builder) -> None:
    """
    Register subscription info from a SubscriptionBuilder so the dispatcher
    can correctly parse arrays and show aliases in PUBLISH packets.

    Call this AFTER building subscriptions but BEFORE connecting to device.

    Parameters
    ----------
    sub_builder : SubscriptionBuilder
        The subscription builder used to generate subscription packets.
    """
    global _subscription_registry, _alias_registry

    _subscription_registry = []
    _alias_registry = []

    for entry in sub_builder._entries:
        # Look up instance to find el_cnt
        ctx_id = entry.ctx_id
        m_type = entry.mem_type
        inst_idx = entry.inst_idx

        # Try to find the instance in the Code's memory contexts
        code = sub_builder.code
        if ctx_id == 0:
            ctx = code.user_ctx
        elif ctx_id == 1:
            ctx = code.blocks_ctx
        else:
            _subscription_registry.append(1)
            _alias_registry.append(f"ctx{ctx_id}:idx{inst_idx}")
            continue

        # Find instance by index in context storage
        found = False
        for inst in ctx.storage[mem_types_t(int(m_type))]:
            if inst.idx == inst_idx:
                el_cnt = 1
                for d in inst.dims:
                    el_cnt *= d
                _subscription_registry.append(max(el_cnt, 1))
                _alias_registry.append(inst.alias or f"idx{inst_idx}")
                found = True
                break

        if not found:
            _subscription_registry.append(1)
            _alias_registry.append(f"idx{inst_idx}")


def register_subscriptions_manual(entries: list) -> None:
    """
    Manually register subscription entries for PUBLISH parsing.

    Parameters
    ----------
    entries : list of dict
        Each dict: {"alias": str, "el_cnt": int}
        In the same order as subscriptions were added.

    Example:
        register_subscriptions_manual([
            {"alias": "selector",  "el_cnt": 1},
            {"alias": "enable",    "el_cnt": 1},
            {"alias": "gains",     "el_cnt": 10},
            {"alias": "ton.Q",     "el_cnt": 1},
        ])
    """
    global _subscription_registry, _alias_registry
    _subscription_registry = [e["el_cnt"] for e in entries]
    _alias_registry = [e["alias"] for e in entries]


# ═══════════════════════════════════════════════════════════════════
# Main dispatcher
# ═══════════════════════════════════════════════════════════════════

# Optional user callbacks per packet type
_user_callbacks: dict[int, List[Callable]] = {
    HEADER_PUBLISH:    [],
    HEADER_ERROR_LOG:  [],
    HEADER_STATUS_LOG: [],
}


def on_publish(callback: Callable[[List[PublishEntry]], None]) -> None:
    """Register a callback for PUBLISH (0xD0) packets. Receives list of PublishEntry."""
    _user_callbacks[HEADER_PUBLISH].append(callback)


def on_error(callback: Callable[[List[ErrorLogEntry]], None]) -> None:
    """Register a callback for ERROR_LOG (0xE0) packets. Receives list of ErrorLogEntry."""
    _user_callbacks[HEADER_ERROR_LOG].append(callback)


def on_status(callback: Callable[[List[StatusLogEntry]], None]) -> None:
    """Register a callback for STATUS_LOG (0xE1) packets. Receives list of StatusLogEntry."""
    _user_callbacks[HEADER_STATUS_LOG].append(callback)


def dispatch_message(data: bytearray, quiet: bool = False) -> None:
    """
    Parse a raw BLE notification and print formatted output.

    This is the main entry point — use as the bleak notification handler:

        await client.start_notify(uuid, lambda s, d: dispatch_message(d))

    Parameters
    ----------
    data : bytearray
        Raw notification bytes from the emulator.
    quiet : bool
        If True, suppress printing (still calls registered callbacks).
    """
    if not data:
        return

    header = data[0]
    payload = bytes(data[1:])

    # ── RAW mode: just dump the whole packet as hex ──────────────
    if _display_mode == DisplayMode.RAW:
        hex_str = " ".join(f"{b:02X}" for b in data)
        _HEADER_TAG = {
            HEADER_PUBLISH:    "PUB",
            HEADER_ERROR_LOG:  "ERR",
            HEADER_STATUS_LOG: "STS",
        }
        tag = _HEADER_TAG.get(header, f"0x{header:02X}")
        if not quiet:
            print(f"[{tag}] ({len(data):>3}B) {hex_str}")
        # still fire callbacks with parsed data
        if header == HEADER_PUBLISH:
            entries = _parse_publish(payload)
            for cb in _user_callbacks[HEADER_PUBLISH]:
                cb(entries)
        elif header == HEADER_ERROR_LOG:
            entries = _parse_error_log(payload)
            for cb in _user_callbacks[HEADER_ERROR_LOG]:
                cb(entries)
        elif header == HEADER_STATUS_LOG:
            entries = _parse_status_log(payload)
            for cb in _user_callbacks[HEADER_STATUS_LOG]:
                cb(entries)
        return

    # ── PRETTY mode (default) ───────────────────────────────────
    if header == HEADER_PUBLISH:
        entries = _parse_publish(payload)
        if not quiet:
            print(_format_publish(entries))
        for cb in _user_callbacks[HEADER_PUBLISH]:
            cb(entries)

    elif header == HEADER_ERROR_LOG:
        entries = _parse_error_log(payload)
        if not quiet:
            print(_format_error_log(entries))
        for cb in _user_callbacks[HEADER_ERROR_LOG]:
            cb(entries)

    elif header == HEADER_STATUS_LOG:
        entries = _parse_status_log(payload)
        if not quiet:
            print(_format_status_log(entries))
        for cb in _user_callbacks[HEADER_STATUS_LOG]:
            cb(entries)

    else:
        # Unknown header — print raw hex regardless of mode
        hex_str = " ".join(f"{b:02X}" for b in data)
        if not quiet:
            print(f"{_C.DIM}[UNKNOWN 0x{header:02X}] {len(data)} bytes: {hex_str}{_C.RESET}")


# ═══════════════════════════════════════════════════════════════════
# Convenience: drop-in replacement for bleak notification handler
# ═══════════════════════════════════════════════════════════════════

def notification_handler(sender, data: bytearray) -> None:
    """
    Drop-in replacement for bleak notification_handler.

    Usage in send_message.py:
        from MessageDispatch import notification_handler
        await client.start_notify(read_char.uuid, notification_handler)
    """
    dispatch_message(data)
