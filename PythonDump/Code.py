"""
Code.py — Unified block-programming environment.

Combines the old Code (packet generation, block factory, ref resolution)
with the old TestSpace (auto-idx, topological sort, generate pipeline).

Usage::

    from Code import Code
    from Enums import mem_types_t

    code = Code()

    # Variables — plain scalars or arrays
    code.var(mem_types_t.MEM_F, "speed", data=10.0)
    code.var(mem_types_t.MEM_F, "result", data=0.0)
    code.var(mem_types_t.MEM_B, "enable", data=True)
    code.var(mem_types_t.MEM_F, "arr", data=[1,2,3,4,5], dims=[5])

    # Blocks — no idx needed (auto-assigned), refs as "alias" strings
    clk = code.add_clock(period_ms=1000, width_ms=500, en="enable")
    loop = code.add_for(expr="i=0; i<10; i+=1", en=clk.out[0])
    m   = code.add_math(expression='"speed" * 2.0', en=loop.out[0])
    code.add_set(target="result", value=m.out[1])
    l   = code.add_logic(expression='"result" > 10.0', en=m.out[1])

    # Generate (auto-sort, reindex, write hex dump)
    code.generate("test_dump.txt")
"""

from __future__ import annotations
import io
import re
import struct
from typing import List, Dict, Optional, Union, TYPE_CHECKING
from dataclasses import dataclass, field

from Enums import packet_header_t, mem_types_t
from Mem import mem_context_t
from MemAcces import AccessManager, Ref
from Block import Block

if TYPE_CHECKING:
    # Imported only for type checkers / IntelliSense to avoid runtime circular imports
    from BlockMath import BlockMath
    from BlockLogic import BlockLogic
    from BlockSet import BlockSet
    from BlockFor import BlockFor
    from BlockClock import BlockClock
    from BlockTimer import BlockTimer
    from BlockCounter import BlockCounter
    from BlockSelector import BlockSelector

# ============================================================================
# Context IDs
# ============================================================================
CTX_USER   = 0   # Context 0: User-created variables
CTX_BLOCKS = 1   # Context 1: Block outputs (hidden from API)

# ============================================================================
# Auto-idx counter
# ============================================================================

class _IdxCounter:
    """Auto-incrementing counter (high start so no clash before reindex)."""
    def __init__(self, start: int = 1000):
        self._next = start

    def __call__(self) -> int:
        val = self._next
        self._next += 1
        return val

# ============================================================================
# CODE
# ============================================================================

@dataclass
class Code:
    """
    Unified block-programming environment.

    * ``var()``          — add user variables
    * ``add_math()``     — add a Math block
    * ``add_logic()``    — add a Logic block
    * ``add_set()``      — add a Set block
    * ``add_for()``      — add a For block
    * ``add_clock()``    — add a Clock block
    * ``add_timer()``    — add a Timer block
    * ``add_counter()``  — add a Counter block
    * ``add_selector()`` — add a Selector block
    * ``generate()``     — sort → reindex → write hex dump

    All ``add_*`` methods accept string aliases for refs
    (auto-wrapped in ``Ref``).  Math / Logic expressions support
    ``"alias"`` and ``"alias"[idx]`` and ``blk_N.out[M]`` tokens.
    """

    user_ctx:   mem_context_t
    blocks_ctx: mem_context_t
    blocks:     Dict[int, Block] = field(default_factory=dict)
    _manager:   AccessManager    = field(default=None)
    _idx:       _IdxCounter      = field(default=None)

    def __init__(self):
        self.user_ctx   = mem_context_t(ctx_id=CTX_USER)
        self.blocks_ctx = mem_context_t(ctx_id=CTX_BLOCKS)
        self.blocks     = {}

        # AccessManager singleton
        AccessManager.reset()
        self._manager = AccessManager.get_instance()
        self._manager.register_context(self.user_ctx)
        self._manager.register_context(self.blocks_ctx)

        # auto-idx
        self._idx = _IdxCounter()

    # ====================================================================
    # Variables
    # ====================================================================

    def var(self,
            var_type: mem_types_t,
            alias: str,
            data=None,
            dims: Optional[List[int]] = None):
        """
        Add a user variable to Context 0.

        :param var_type: MEM_F, MEM_B, MEM_U8, MEM_U16, MEM_U32, MEM_I32 …
        :param alias:    Unique name
        :param data:     Initial value (scalar or list for arrays)
        :param dims:     Array dimensions (e.g. [5] for 1-D)
        """
        self.user_ctx.add(type=var_type, alias=alias, data=data, dims=dims)

    # kept for backward compat
    add_variable = var

    def ref(self, alias: str) -> Ref:
        """Get a Ref to a user variable by alias."""
        return Ref(alias)

    # ====================================================================
    # Internal helpers — resolve aliases / expression refs
    # ====================================================================

    @staticmethod
    def _resolve_ref(value):
        """Ref|str → Ref,  int/float/None → as-is."""
        if value is None or isinstance(value, (int, float, Ref)):
            return value
        if isinstance(value, str):
            return Ref(value)
        raise TypeError(f"Cannot resolve {type(value)} to Ref")

    @staticmethod
    def _extract_refs_from_expr(expression: str):
        """
        Scan *expression* for ``"alias"`` / ``"alias"[idx]`` /
        ``blk_N.out[M]`` tokens and replace each with ``in_N``.

        Returns ``(rewritten_expr, connections_list)``.
        """
        refs: list = []
        alias_to_idx: dict = {}

        def _next_idx():
            return len(refs) + 1

        def _register(alias_key: str, ref_obj):
            if alias_key not in alias_to_idx:
                idx = _next_idx()
                alias_to_idx[alias_key] = idx
                refs.append(ref_obj)
            return f'in_{alias_to_idx[alias_key]}'

        result = expression

        # 1) block output tokens:  blk_<N>.out[<M>]
        blk_pat = re.compile(r'blk_(\d+)\.out\[(\d+)\]')
        def _replace_blk(m):
            bidx, oidx = int(m.group(1)), int(m.group(2))
            key = f'{bidx}_q_{oidx}'
            return _register(key, Ref(key))
        result = blk_pat.sub(_replace_blk, result)

        # 2) quoted aliases with optional array index:  "alias"[idx]
        quoted_pat = re.compile(r'"([^"]+)"(?:\[(\d+)\])?')
        def _replace_quoted(m):
            alias = m.group(1)
            arr_idx = m.group(2)
            if arr_idx is not None:
                key = f'{alias}[{arr_idx}]'
                ref = Ref(alias)[int(arr_idx)]
            else:
                key = alias
                ref = Ref(alias)
            return _register(key, ref)
        result = quoted_pat.sub(_replace_quoted, result)

        return result, refs

    # ====================================================================
    # FOR expression parser
    # ====================================================================

    @staticmethod
    def _parse_for_expr(expr: str):
        """
        Parse C-style ``"i=0; i<10; i+=1"`` into
        ``(start, cond_str, limit, op_str, step)``.
        """
        parts = [p.strip() for p in expr.split(';')]
        if len(parts) != 3:
            raise ValueError(
                f"For expression must have 3 semicolon-separated parts, "
                f"got {len(parts)}: '{expr}'"
            )

        # init
        init_m = re.match(r'(\w+)\s*=\s*(.+)', parts[0])
        if not init_m:
            raise ValueError(f"Cannot parse init part: '{parts[0]}'")
        start_tok = init_m.group(2).strip()

        # condition
        cond_m = re.match(r'\w+\s*(<=|>=|<|>)\s*(.+)', parts[1])
        if not cond_m:
            raise ValueError(f"Cannot parse condition part: '{parts[1]}'")
        cond_str  = cond_m.group(1)
        limit_tok = cond_m.group(2).strip()

        # step
        step_m = re.match(r'\w+\s*([+\-*/])=\s*(.+)', parts[2])
        if not step_m:
            step_m2 = re.match(r'\w+\s*=\s*\w+\s*([+\-*/])\s*(.+)', parts[2])
            if not step_m2:
                raise ValueError(f"Cannot parse step part: '{parts[2]}'")
            step_m = step_m2
        op_str   = step_m.group(1)
        step_tok = step_m.group(2).strip()

        def _to_val(tok):
            tok = tok.strip('"').strip("'")
            try:
                return float(tok)
            except ValueError:
                return Ref(tok)

        return _to_val(start_tok), cond_str, _to_val(limit_tok), op_str, _to_val(step_tok)

    # ====================================================================
    # Block factory methods  (no idx required — auto-assigned)
    # ====================================================================

    def add_math(self,
                 expression: str,
                 connections=None,
                 en=None,
                 idx: Optional[int] = None) -> 'BlockMath':
        """
        Add a Math block.

        Expression can contain ``"alias"`` tokens (auto-resolved)::

            code.add_math(expression='"speed" * 3.14 + "offset"')

        Or classic ``in_N`` with explicit connections::

            code.add_math(expression="in_1 * 3.14",
                          connections=[Ref("speed")])
        """
        from BlockMath import BlockMath
        if idx is None:
            idx = self._idx()
        en = self._resolve_ref(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        block = BlockMath(
            idx=idx, ctx=self.blocks_ctx,
            expression=expression,
            connections=connections if connections else None,
            en=en,
        )
        return self._register_block(block)

    def add_logic(self,
                  expression: str,
                  connections=None,
                  en=None,
                  idx: Optional[int] = None) -> 'BlockLogic':
        """
        Add a Logic block.

        Expression can contain ``"alias"`` tokens::

            code.add_logic(expression='("temp" > 50) && ("press" < 100)')

        Block outputs: ``blk_3.out[1] > 10``.
        """
        from BlockLogic import BlockLogic
        if idx is None:
            idx = self._idx()
        en = self._resolve_ref(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        block = BlockLogic(
            idx=idx, ctx=self.blocks_ctx,
            expression=expression,
            connections=connections if connections else None,
            en=en,
        )
        return self._register_block(block)

    def add_set(self,
                target=None,
                value=None,
                idx: Optional[int] = None) -> 'BlockSet':
        """
        Add a SET block.

        ``target`` / ``value`` accept string alias or Ref::

            code.add_set(target="motor_speed", value=math.out[1])
        """
        from BlockSet import BlockSet
        if idx is None:
            idx = self._idx()
        target = self._resolve_ref(target)
        value  = self._resolve_ref(value)
        block = BlockSet(idx=idx, ctx=self.blocks_ctx, target=target, value=value)
        return self._register_block(block)

    def add_for(self,
                expr: str = None,
                start=0.0, limit=10.0, step=1.0,
                condition=None, operator=None,
                en=None,
                chain_len: int = 0,
                idx: Optional[int] = None) -> 'BlockFor':
        """
        Add a FOR loop block.

        C-style expression (preferred)::

            code.add_for(expr="i=0; i<10; i+=1", en=clk.out[0])

        ``chain_len`` is auto-computed during ``generate()``.
        """
        from BlockFor import BlockFor, _resolve_condition, _resolve_operator
        if idx is None:
            idx = self._idx()
        en = self._resolve_ref(en)

        if expr is not None:
            start, condition, limit, operator, step = self._parse_for_expr(expr)

        condition = _resolve_condition(condition)
        operator  = _resolve_operator(operator)
        block = BlockFor(
            idx=idx, ctx=self.blocks_ctx, chain_len=chain_len,
            start=start, limit=limit, step=step,
            condition=condition, operator=operator, en=en,
        )
        return self._register_block(block)

    def add_clock(self,
                  period_ms=1000.0,
                  width_ms=500.0,
                  en=None,
                  idx: Optional[int] = None) -> 'BlockClock':
        """
        Add a Clock / PWM block.

        ``en`` accepts string alias or Ref.
        """
        from BlockClock import BlockClock
        if idx is None:
            idx = self._idx()
        en        = self._resolve_ref(en)
        period_ms = self._resolve_ref(period_ms)
        width_ms  = self._resolve_ref(width_ms)
        block = BlockClock(
            idx=idx, ctx=self.blocks_ctx,
            period_ms=period_ms, width_ms=width_ms, en=en,
        )
        return self._register_block(block)

    def add_timer(self,
                  timer_type=None,
                  pt=1000,
                  en=None,
                  rst=None,
                  idx: Optional[int] = None) -> 'BlockTimer':
        """
        Add a Timer block.

        :param timer_type: TON, TOF, TP, etc.  (default: TON)
        :param pt: Preset time in ms (constant or Ref)
        """
        from BlockTimer import BlockTimer, TimerType
        if idx is None:
            idx = self._idx()
        if timer_type is None:
            timer_type = TimerType.TON
        en  = self._resolve_ref(en)
        rst = self._resolve_ref(rst)
        pt  = self._resolve_ref(pt)
        block = BlockTimer(
            idx=idx, ctx=self.blocks_ctx,
            timer_type=timer_type, pt=pt, en=en, rst=rst,
        )
        return self._register_block(block)

    def add_counter(self,
                    cu=None, cd=None, reset=None,
                    step=1.0, limit_max=100.0, limit_min=0.0,
                    start_val=0.0, mode=None,
                    idx: Optional[int] = None) -> 'BlockCounter':
        """
        Add a Counter block.

        :param cu/cd/reset: Count-Up / Count-Down / Reset inputs (str or Ref)
        :param mode: ON_RISING (default) or WHEN_ACTIVE
        """
        from BlockCounter import BlockCounter, CounterMode
        if idx is None:
            idx = self._idx()
        if mode is None:
            mode = CounterMode.ON_RISING
        cu    = self._resolve_ref(cu)
        cd    = self._resolve_ref(cd)
        reset = self._resolve_ref(reset)
        step      = self._resolve_ref(step)
        limit_max = self._resolve_ref(limit_max)
        limit_min = self._resolve_ref(limit_min)
        block = BlockCounter(
            idx=idx, ctx=self.blocks_ctx,
            cu=cu, cd=cd, reset=reset,
            step=step, limit_max=limit_max, limit_min=limit_min,
            start_val=start_val, mode=mode,
        )
        return self._register_block(block)

    def add_selector(self,
                     selector=None,
                     options=None,
                     idx: Optional[int] = None) -> 'BlockSelector':
        """
        Add a Selector / Multiplexer block.

        ``selector`` and ``options`` accept string aliases::

            code.add_selector(selector="mode",
                              options=["speed_low", "speed_med", "speed_high"])
        """
        from BlockSelector import BlockSelector
        if idx is None:
            idx = self._idx()
        if isinstance(selector, str):
            selector = Ref(selector)
        if options and isinstance(options[0], str):
            options = [Ref(o) if isinstance(o, str) else o for o in options]
        block = BlockSelector(
            idx=idx, ctx=self.blocks_ctx,
            selector=selector, options=options or [],
        )
        return self._register_block(block)

    # ====================================================================
    # Block management
    # ====================================================================

    def _register_block(self, block: Block) -> Block:
        """Register a block (internal)."""
        if block.idx in self.blocks:
            raise ValueError(f"Block with index {block.idx} already exists")
        self.blocks[block.idx] = block
        return block

    # Backward compat alias
    add_block = _register_block

    def create_block(self, idx: int, block_type) -> Block:
        """Create a bare Block with correct context (advanced)."""
        return Block(idx=idx, block_type=block_type, ctx=self.blocks_ctx)

    def get_block(self, idx: int) -> Optional[Block]:
        return self.blocks.get(idx)

    def get_blocks_sorted(self) -> List[Block]:
        return [self.blocks[i] for i in sorted(self.blocks.keys())]

    @property
    def block_count(self) -> int:
        return len(self.blocks)

    # ====================================================================
    # Sorting pipeline
    # ====================================================================

    def _sort(self):
        """Topological sort + reindex + auto chain_len.  Mutates in-place."""
        from algorithm import sort_and_reindex
        sorted_blocks = sort_and_reindex(self.blocks)
        self.blocks = {b.idx: b for b in sorted_blocks}

    # ====================================================================
    # Generation pipeline
    # ====================================================================

    def generate(self,
                 filename: str = "test_dump.txt",
                 raw: bool = False,
                 sort: bool = True,
                 verbose: bool = True):
        """
        Sort blocks → reindex → write hex dump to *filename*.

        :param filename: Output file path.
        :param raw:      If True, write without comments (for BLE send).
        :param sort:     If True (default), run topological sort + reindex.
        :param verbose:  Print summary to stdout.
        """
        from FullDump import FullDump

        if sort:
            self._sort()

        dump = FullDump(self)

        with open(filename, "w") as f:
            if raw:
                dump.write_raw(f)
            else:
                dump.write(f)

        if verbose:
            self._print_summary(filename)

    def generate_raw_string(self, sort: bool = True) -> str:
        """Return raw hex dump as a string (useful for BLE transmission)."""
        from FullDump import FullDump
        if sort:
            self._sort()
        dump = FullDump(self)
        buf = io.StringIO()
        dump.write_raw(buf)
        return buf.getvalue()

    # ====================================================================
    # Packet generation (low-level, kept for advanced use / FullDump)
    # ====================================================================

    def generate_code_cfg_packet(self) -> bytes:
        return struct.pack('<BH',
                           packet_header_t.PACKET_H_CODE_CFG,
                           self.block_count)

    def generate_packets(self, manager: Optional[AccessManager] = None) -> List[bytes]:
        """
        Generate all packets in correct order:
        1. Code config  2. Block headers  3. Inputs  4. Outputs  5. Data
        """
        if manager is None:
            manager = self._manager

        packets = []
        blocks_sorted = self.get_blocks_sorted()

        packets.append(self.generate_code_cfg_packet())

        for block in blocks_sorted:
            header = struct.pack('<BH',
                                 packet_header_t.PACKET_H_BLOCK_HEADER,
                                 block.idx)
            packets.append(header + block.pack_cfg())

        for block in blocks_sorted:
            packets.extend(block.pack_inputs(manager))

        for block in blocks_sorted:
            packets.extend(block.pack_outputs(manager))

        for block in blocks_sorted:
            if hasattr(block, 'pack_data'):
                data_packets = block.pack_data()
                if data_packets:
                    packets.extend(data_packets)

        return packets

    def generate_packets_hex(self, manager: Optional[AccessManager] = None) -> List[str]:
        if manager is None:
            manager = self._manager
        return [pkt.hex().upper() for pkt in self.generate_packets(manager)]

    def print_packets(self, manager: Optional[AccessManager] = None):
        """Print all packets with labels (debug helper)."""
        if manager is None:
            manager = self._manager
        blocks_sorted = self.get_blocks_sorted()

        print(f"\n{'='*60}")
        print(f"Code Packets ({self.block_count} blocks)")
        print('='*60)

        cfg_pkt = self.generate_code_cfg_packet()
        print(f"\n[CODE_CFG] {cfg_pkt.hex().upper()}")

        print(f"\n--- Block Headers ---")
        for block in blocks_sorted:
            header = struct.pack('<BH', packet_header_t.PACKET_H_BLOCK_HEADER, block.idx)
            pkt = header + block.pack_cfg()
            print(f"  [{block.idx}] {block.block_type.name}: {pkt.hex().upper()}")

        print(f"\n--- Block Inputs ---")
        for block in blocks_sorted:
            in_pkts = block.pack_inputs(manager)
            for i, pkt in enumerate(in_pkts):
                print(f"  [{block.idx}] Input {i}: {pkt.hex().upper()}")

        print(f"\n--- Block Outputs ---")
        for block in blocks_sorted:
            out_pkts = block.pack_outputs(manager)
            for i, pkt in enumerate(out_pkts):
                print(f"  [{block.idx}] Output {i}: {pkt.hex().upper()}")

        print(f"\n--- Block Data ---")
        for block in blocks_sorted:
            if hasattr(block, 'pack_data'):
                data_pkts = block.pack_data()
                if data_pkts:
                    for pkt in data_pkts:
                        pid = pkt[4] if len(pkt) > 4 else 0
                        print(f"  [{block.idx}] Data(0x{pid:02X}): {pkt.hex().upper()}")

        print('='*60)

    # ====================================================================
    # Summary
    # ====================================================================

    def _print_summary(self, filename: str):
        blocks = self.get_blocks_sorted()
        print(f"\n{'='*60}")
        print(f"  Code — generated {filename}")
        print(f"{'='*60}")
        print(f"  Variables : {len(self.user_ctx.alias_map)}")
        print(f"  Blocks    : {len(blocks)}")
        print(f"  Execution order:")
        for b in blocks:
            extra = ""
            if hasattr(b, 'chain_len'):
                extra = f"  chain_len={b.chain_len}"
            print(f"    [{b.idx}] {b.block_type.name}{extra}")
        print(f"{'='*60}\n")

    def __repr__(self) -> str:
        return (f"Code(blocks={self.block_count}, "
                f"user_vars={len(self.user_ctx.alias_map)}, "
                f"block_outputs={len(self.blocks_ctx.alias_map)})")


# ============================================================================
# DEMO
# ============================================================================

if __name__ == "__main__":
    from MemAcces import Ref

    print("=" * 60)
    print("Code Demo")
    print("=" * 60)

    code = Code()

    # --- Variables ---
    code.var(mem_types_t.MEM_F, "speed",     data=3.3)
    code.var(mem_types_t.MEM_F, "factor",    data=2.0)
    code.var(mem_types_t.MEM_F, "result",    data=0.0)
    code.var(mem_types_t.MEM_B, "enable",    data=True)
    code.var(mem_types_t.MEM_F, "threshold", data=50.0)

    # --- Blocks (no idx, any order) ---
    c = code.add_clock(period_ms=5000, width_ms=1000, en="enable")
    l = code.add_logic(expression='"speed" > "threshold"', en="enable")
    m = code.add_math(expression='"speed" * "factor"', en=c.out[0])
    s = code.add_set(target="result", value=m.out[1])
    f = code.add_for(expr="i=0; i<5; i+=1", en=m.out[1])
    m2 = code.add_math(expression='100+100', en=f.out[0])
    s1 = code.add_set(target="result", value=m2.out[1])
    s2 = code.add_set(target="result", value=m2.out[0])
    s3 = code.add_set(target="result", value=m2.out[1])
    m3 = code.add_math(expression='"result" * 3.0', en=f.out[0])



    # --- Generate ---
    code.generate("test_dump.txt")
