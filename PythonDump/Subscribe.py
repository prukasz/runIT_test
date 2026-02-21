import struct
from typing import Union
from dataclasses import dataclass

from Enums import (
    packet_header_t,
    emu_order_t,
    mem_types_t,
)
from MemAcces import AccessManager, Ref


# ============================================================================
# Subscription entry – resolved reference to a single instance
# ============================================================================
@dataclass
class SubscriptionEntry:
    """One subscription target: context + type + instance index."""
    ctx_id: int
    mem_type: mem_types_t
    inst_idx: int

    def pack(self) -> bytes:
        """Serialize to 3-byte wire format: [type: u8][inst_idx: u16 LE]."""
        return struct.pack('<BH', int(self.mem_type), self.inst_idx)


# ============================================================================
# SubscriptionBuilder – high-level API
# ============================================================================
class SubscriptionBuilder:
    """
    Collects instance aliases (or raw references) and produces the two
    subscription packets that the emulator parser expects.

    Parameters
    ----------
    code : Code
        Code object that owns user_ctx / blocks_ctx for alias resolution.
    pkt_size : int
        Maximum payload size per SUBSCRIPTION_ADD packet (default 512).
    """

    def __init__(self, code, pkt_size: int = 512):
        self.code = code
        self.pkt_size = pkt_size
        self._entries: list[SubscriptionEntry] = []
        self._manager: AccessManager = code._manager

    # ------------------------------------------------------------------
    # Public API – adding subscriptions
    # ------------------------------------------------------------------

    def add(self, target: Union[str, Ref]) -> 'SubscriptionBuilder':
        """
        Subscribe to an instance by alias string or Ref object.

        Accepts:
          - ``str``  — plain alias (e.g. ``"temperature"``)
          - ``Ref``  — reference object (e.g. ``ton.out[0]``, ``Ref("counter")``)

        Resolves through AccessManager to find context, type & index.
        """
        alias = target.alias if isinstance(target, Ref) else target
        ctx_id, m_type, idx, _ = self._manager.resolve_alias(alias)
        self._entries.append(SubscriptionEntry(ctx_id, m_type, idx))
        return self

    # ------------------------------------------------------------------
    # Packet generation
    # ------------------------------------------------------------------

    def _pack_init(self) -> bytes:
        """
        Build SUBSCRIPTION_INIT packet.
        Layout: [header 0xC0][sub_list_size: u16 LE]
        """
        header = struct.pack('<B', packet_header_t.PACKET_H_SUBSCRIPTION_INIT)
        payload = struct.pack('<H', len(self._entries))
        return header + payload

    def _pack_add_packets(self) -> list[bytes]:
        """
        Build one or more SUBSCRIPTION_ADD packets.
        Layout per packet: [header 0xC1][ctx: u8][count: u8][ (type:u8, inst_idx:u16) × count ]

        Entries are grouped by context and split across packets when
        the payload would exceed *pkt_size*.
        """
        # Group entries by ctx_id (preserve insertion order)
        groups: dict[int, list[SubscriptionEntry]] = {}
        for entry in self._entries:
            groups.setdefault(entry.ctx_id, []).append(entry)

        packets: list[bytes] = []
        header_byte = struct.pack('<B', packet_header_t.PACKET_H_SUBSCRIPTION_ADD)

        for ctx_id, entries in groups.items():
            # Each entry = 3 bytes (type u8 + inst_idx u16).  Fixed overhead = 2 (ctx + count).
            overhead = 2  # ctx(1) + count(1)
            entry_size = 3
            max_per_pkt = (self.pkt_size - overhead) // entry_size

            # Split into chunks that fit in a single packet
            for start in range(0, len(entries), max_per_pkt):
                chunk = entries[start : start + max_per_pkt]
                count = len(chunk)
                payload = struct.pack('<BB', ctx_id, count)
                for e in chunk:
                    payload += e.pack()
                packets.append(header_byte + payload)

        return packets

    def build(self) -> tuple[bytes, list[bytes]]:
        """
        Returns
        -------
        init_packet : bytes
            The SUBSCRIPTION_INIT packet (with header byte).
        add_packets : list[bytes]
            One or more SUBSCRIPTION_ADD packets (with header byte).
        """
        if not self._entries:
            raise ValueError("No subscriptions added – call .add() first")
        return self._pack_init(), self._pack_add_packets()

    # ------------------------------------------------------------------
    # FullDump integration helpers
    # ------------------------------------------------------------------

    def collect_sections(self) -> list[tuple[str, list[tuple[bytes, str]], list[emu_order_t]]]:
        """
        Return sections compatible with FullDump._collect_sections() format:
        ``[(title, [(pkt_bytes, comment), ...], [order, ...])]``

        Produces two sections:
            1. Subscription Init  – single init packet + ORD_PARSE_SUBSCRIPTION_INIT
            2. Subscription Add   – one or more add packets + ORD_PARSE_SUBSCRIPTION_ADD
        """
        init_pkt, add_pkts = self.build()

        sections = []

        # Section: init
        sections.append((
            "Sub Init",
            [(init_pkt, f"SUB_INIT (max {len(self._entries)} entries)")],
            [emu_order_t.ORD_PARSE_SUBSCRIPTION_INIT],
        ))

        # Section: add
        add_items = []
        for i, pkt in enumerate(add_pkts):
            ctx_id = pkt[1]  # first payload byte after header
            count  = pkt[2]
            add_items.append((pkt, f"SUB_ADD [{i}] ctx={ctx_id} cnt={count}"))

        sections.append((
            "Sub Add",
            add_items,
            [emu_order_t.ORD_PARSE_SUBSCRIPTION_ADD],
        ))

        return sections

