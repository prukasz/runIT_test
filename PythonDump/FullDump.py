"""
Outputs hex packets in correct order for C parser:
1. Context configurations
2. Variable instances 
3. Variable data (scalars and arrays)
4. Code configuration
5. Block headers
6. Block inputs
7. Block outputs  
8. Block data
9. Loop control orders
"""

import struct
from typing import TextIO, List

from Enums import emu_order_t, packet_header_t
from Code import Code

# =============================================================================
# FULL DUMP CLASS
# =============================================================================
class FullDump:
    def __init__(self, code: Code):
        self.code = code

    # -------------------------------------------------------------------------
    # Helpers
    # -------------------------------------------------------------------------

    @staticmethod
    def _prefix(header: int, payload: bytes) -> bytes:
        """Prepend a 1-byte packet header to *payload*."""
        return struct.pack('<B', header) + payload

    @staticmethod
    def _order_hex(order: emu_order_t) -> str:
        return struct.pack("<H", order.value).hex().upper()

    # -------------------------------------------------------------------------
    # Single source of truth: ordered section list
    # -------------------------------------------------------------------------

    def _collect_sections(self, include_loop_init=True, include_loop_start=True):
        """
        Build complete ordered list of sections.

        Returns ``[(title, [(pkt_bytes, comment), ...], [order, ...])]``.
        Every writer method consumes this list in its own format.
        """
        manager = self.code._manager
        blocks_sorted = self.code.get_blocks_sorted()
        ctxs = [("CTX0", self.code.user_ctx),
                ("CTX1", self.code.blocks_ctx)]
        sections = []

        # Helper: collect packets from both contexts
        def _both_ctx(gen_fn, header_byte, tag):
            pkts = []
            for name, ctx in ctxs:
                for i, pkt in enumerate(gen_fn(ctx)):
                    pkts.append((self._prefix(header_byte, pkt),
                                 f"{name} {tag} [{i}]"))
            return pkts

        # 1. Context configs
        cfg_pkts = [(self._prefix(packet_header_t.PACKET_H_CONTEXT_CFG,
                                  ctx.packet_generate_cfg()), f"{n} CFG")
                    for n, ctx in ctxs]
        sections.append(("Context Configs", cfg_pkts,
                         [emu_order_t.ORD_PARSE_CONTEXT_CFG]))

        # 2. Instance definitions
        sections.append(("Instance Defs",
                         _both_ctx(lambda c: c.generate_packets_instances(),
                                   packet_header_t.PACKET_H_INSTANCE, "INST"),
                         [emu_order_t.ORD_PARSE_VARIABLES]))

        # 3. Scalar data
        sections.append(("Scalar Data",
                         _both_ctx(lambda c: c.generate_packets_scalar_data(),
                                   packet_header_t.PACKET_H_INSTANCE_SCALAR_DATA, "SDATA"),
                         [emu_order_t.ORD_PARSE_VARIABLES_S_DATA]))

        # 4. Array data
        sections.append(("Array Data",
                         _both_ctx(lambda c: c.generate_packets_array_data(),
                                   packet_header_t.PACKET_H_INSTANCE_ARR_DATA, "ADATA"),
                         [emu_order_t.ORD_PARSE_VARIABLES_ARR_DATA]))

        # 5. Code config
        sections.append(("Code Config",
                         [(self.code.generate_code_cfg_packet(), "CODE_CFG")],
                         [emu_order_t.ORD_PARSE_CODE_CFG]))

        # 6. Block headers
        blk_hdrs = [(self._prefix(packet_header_t.PACKET_H_BLOCK_HEADER,
                                  blk.pack_cfg()),
                     f"BLK[{blk.idx}] {blk.block_type.name} HDR")
                    for blk in blocks_sorted]
        sections.append(("Block Hdrs", blk_hdrs,
                         [emu_order_t.ORD_PARSE_BLOCK_HEADER]))

        # 7. Block inputs
        blk_in = [(pkt, f"BLK[{blk.idx}] INPUT[{i}]")
                  for blk in blocks_sorted
                  for i, pkt in enumerate(blk.pack_inputs(manager))]
        sections.append(("Block Input", blk_in,
                         [emu_order_t.ORD_PARSE_BLOCK_INPUTS]))

        # 8. Block outputs
        blk_out = [(pkt, f"BLK[{blk.idx}] OUTPUT[{i}]")
                   for blk in blocks_sorted
                   for i, pkt in enumerate(blk.pack_outputs(manager))]
        sections.append(("Block Output", blk_out,
                         [emu_order_t.ORD_PARSE_BLOCK_OUTPUTS]))

        # 9. Block data
        blk_data = []
        for blk in blocks_sorted:
            if hasattr(blk, 'pack_data'):
                data_pkts = blk.pack_data()
                if data_pkts:
                    for pkt in data_pkts:
                        pid = pkt[4] if len(pkt) > 4 else 0
                        blk_data.append((pkt, f"BLK[{blk.idx}] DATA(0x{pid:02X})"))
        sections.append(("Block Data", blk_data,
                         [emu_order_t.ORD_PARSE_BLOCK_DATA] if blk_data else []))

        # 10. Loop control
        loop_orders = []
        if include_loop_init:
            loop_orders.append(emu_order_t.ORD_EMU_LOOP_INIT)
        if include_loop_start:
            loop_orders.append(emu_order_t.ORD_EMU_LOOP_START)
        sections.append(("Loop Ctrl", [], loop_orders))

        return sections

    # -------------------------------------------------------------------------
    # Writers
    # -------------------------------------------------------------------------

    def write(self, writer: TextIO,
              include_loop_init: bool = True,
              include_loop_start: bool = True):
        """Generate commented dump for human inspection."""

        sections = self._collect_sections(include_loop_init, include_loop_start)
        for num, (title, pkts, orders) in enumerate(sections, 1):
            label = f"# SECTION {num}: {title:<14s} #"
            bar = "#" + "-" * (len(label) - 2) + "#"
            writer.write(bar + "\n")
            writer.write(label + "\n")
            writer.write(bar + "\n")

            for pkt, comment in pkts:
                writer.write(f"#{comment}# {pkt.hex().upper()}\n")
            for order in orders:
                writer.write(f"#ORDER {order.name}# {self._order_hex(order)}\n")
            writer.write("\n")

        writer.write("#" + "=" * 14 + "#\n")
        writer.write("# END OF DUMP #\n")
        writer.write("#" + "=" * 14 + "#\n")

    def write_raw(self, writer: TextIO,
                  include_loop_init: bool = True,
                  include_loop_start: bool = True):
        """Generate minimal hex-only dump for device transmission."""
        for _, pkts, orders in self._collect_sections(include_loop_init,
                                                      include_loop_start):
            for pkt, _ in pkts:
                writer.write(pkt.hex().upper() + "\n")
            for order in orders:
                writer.write(self._order_hex(order) + "\n")

    def get_packets_list(self) -> List[bytes]:
        """Get all data packets (no orders) for programmatic use."""
        return [pkt for _, pkts, _ in self._collect_sections(
                    include_loop_init=False, include_loop_start=False)
                for pkt, _ in pkts]

