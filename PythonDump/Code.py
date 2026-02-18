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
    from BlockInSelector import BlockInSelector
    from BlockQSelector import BlockQSelector
    from BlockLatch import BlockLatch
    from Subscribe import SubscriptionBuilder

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
    * ``add_set()``        — add a Set block
    * ``add_for()``        — add a For block
    * ``add_clock()``      — add a Clock block
    * ``add_timer()``      — add a Timer block
    * ``add_counter()``    — add a Counter block
    * ``add_in_selector()`` — add an IN_SELECTOR block
    * ``add_q_selector()`` — add a Q_SELECTOR block
    * ``add_latch()``      — add a Latch block (SR or RS)
    * ``generate()``       — sort → reindex → write hex dump

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

        # block aliases:  name → Block
        self._block_aliases: Dict[str, Block] = {}

    # ====================================================================
    # Variables
    # ====================================================================

    def var(self,var_type: mem_types_t, alias: str, data=None, dims: Optional[List[int]] = None):
        self.user_ctx.add(type=var_type, alias=alias, data=data, dims=dims)

    def ref(self, alias: str) -> Ref:
        """Get a Ref to a user variable by alias."""
        return Ref(alias)

    # ====================================================================
    # Internal helpers — resolve aliases / expression refs
    # ====================================================================

    def _resolve_ref(self, value):
        """
        Resolve a value to a Ref.

        Accepts:
          - None / int / float / Ref  → returned as-is
          - ``"alias"``               → Ref("alias")  (user variable)
          - ``"name[N]"``             → block output if *name* is a block alias,
                                        otherwise Ref("name")[N]  (array var)
        """
        if value is None or isinstance(value, (int, float, Ref)):
            return value
        if isinstance(value, str):
            m = re.match(r'^(\w+)\[(\d+)\]$', value)
            if m:
                name, out_idx = m.group(1), int(m.group(2))
                if name in self._block_aliases:
                    return self._block_aliases[name].out[out_idx]
                # not a block alias → treat as array variable
                return Ref(name)[out_idx]
            return Ref(value)
        raise TypeError(f"Cannot resolve {type(value)} to Ref")

    def _extract_refs_from_expr(self, expression: str):
        """
        Scan *expression* for quoted tokens and replace with ``in_N``.

        Recognised forms inside the expression string:

        * ``"var"``                    → Ref("var")                  (scalar)
        * ``"var"[2]``                 → Ref("var")[2]               (array, outer idx)
        * ``"var[2]"``                 → Ref("var")[2]               (array, inner idx)
        * ``"blk_alias[1]"``           → block output Ref            (block alias)
        * ``"arr["idx_var"]"``         → Ref("arr")[Ref("idx_var")]  (dynamic index)
        * ``"arr["blk[0]"]"``          → Ref("arr")[blk.out[0]]      (dynamic, block out)
        * ``"arr["other[2]"]"``        → Ref("arr")[Ref("other")[2]] (dynamic, array elem)
        * ``blk_3.out[1]``            → Ref("3_q_1")                (raw numeric block)

        Nesting: an inner ``"..."`` inside an outer ``"..."`` is treated
        as a **dynamic array index**.  The inner part is resolved first
        (scalar var, ``name[N]`` block alias or array element) and used
        as a ``Ref`` index on the outer variable.

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

        def _resolve_inner(text: str):
            """
            Resolve an inner token (the text between quotes) to a Ref.
            Handles: scalar ``var``, ``name[N]`` (block alias or array),
            and nested ``outer["inner"]`` with arbitrary depth.
            """
            # --- nested dynamic index:  name["..."]  ---
            # Find the first  ["  which opens a nested ref.
            # We can't use a simple regex because the inner part may
            # itself contain quotes, so we scan for the bracket-quote pair.
            bracket_quote = text.find('["')
            if bracket_quote != -1:
                outer_name = text[:bracket_quote]
                # outer_name must be a plain identifier
                if outer_name.isidentifier():
                    # Find the matching  "]  at the end, respecting nesting.
                    # Everything between  ["  and  "]  is the inner content.
                    inner_start = bracket_quote + 2   # skip  ["
                    # Scan from inner_start to find the balanced  "]  close.
                    depth = 1
                    k = inner_start
                    while k < len(text):
                        if text[k:k+2] == '["':
                            depth += 1
                            k += 2
                            continue
                        if text[k:k+2] == '"]':
                            depth -= 1
                            if depth == 0:
                                inner_text = text[inner_start:k]
                                # resolve recursively
                                inner_ref = _resolve_inner(inner_text)
                                return Ref(outer_name)[inner_ref]
                            k += 2
                            continue
                        k += 1

            # --- name[N] (static integer index) ---
            idx_m = re.match(r'^(\w+)\[(\d+)\]$', text)
            if idx_m:
                name, idx_val = idx_m.group(1), int(idx_m.group(2))
                if name in self._block_aliases:
                    return self._block_aliases[name].out[idx_val]
                return Ref(name)[idx_val]

            # --- plain scalar ---
            return Ref(text)

        result = expression

        # 1) block output tokens:  blk_<N>.out[<M>]
        blk_pat = re.compile(r'blk_(\d+)\.out\[(\d+)\]')
        def _replace_blk(m):
            bidx, oidx = int(m.group(1)), int(m.group(2))
            key = f'{bidx}_q_{oidx}'
            return _register(key, Ref(key))
        result = blk_pat.sub(_replace_blk, result)

        # 2) Quoted tokens — character-level scan that handles nesting.
        #
        #    Top-level form:  "content"  optionally followed by  [N]
        #    Nested form:     "outer["inner["deep"]"]"
        #
        #    Nesting rule (arbitrary depth):
        #      ["  opens a deeper level   (depth += 1)
        #      "]  closes a level         (depth -= 1)
        #    A plain '"' at depth 1 closes the top-level span.

        def _find_top_level_quoted(text: str, start: int):
            """
            Starting at *start* (which must be a ``"``), find the full
            top-level quoted span, supporting **arbitrary** nesting depth.

            Nesting is delimited by the two-character sequences ``["``
            (open) and ``"]`` (close).

            Returns ``(raw_content, end_pos)`` where *end_pos* is the
            index right after the closing ``"``.
            """
            assert text[start] == '"'
            depth = 1
            j = start + 1
            while j < len(text):
                # Check two-char sequences first (higher priority)
                pair = text[j:j+2]
                if pair == '["':
                    depth += 1
                    j += 2
                    continue
                if pair == '"]':
                    depth -= 1
                    if depth == 0:
                        # The '"' of  "]  is the closing outer quote
                        raw = text[start + 1 : j]
                        return raw, j + 2   # skip past  "]
                    j += 2
                    continue
                # Single '"' at depth 1 → closing outer quote (no nesting)
                if text[j] == '"' and depth == 1:
                    raw = text[start + 1 : j]
                    return raw, j + 1
                j += 1
            # Fallback: unterminated quote — take everything
            raw = text[start + 1:]
            return raw, len(text)

        def _scan_quoted(text: str) -> str:
            """Replace all top-level \"...\" (possibly nested) tokens."""
            out_parts: list = []
            i = 0
            while i < len(text):
                if text[i] == '"':
                    raw, end = _find_top_level_quoted(text, i)
                    # optional trailing [idx] OUTSIDE the quotes
                    outer_idx = None
                    trail_m = re.match(r'\[(\d+)\]', text[end:])
                    if trail_m:
                        outer_idx = int(trail_m.group(1))
                        end += trail_m.end()
                    # resolve content
                    ref = _resolve_inner(raw)
                    if outer_idx is not None and not ref.indices:
                        # outer [idx] applied to a plain alias
                        alias_name = ref.alias
                        if alias_name in self._block_aliases:
                            ref = self._block_aliases[alias_name].out[outer_idx]
                            key = f'__blk__{alias_name}[{outer_idx}]'
                            out_parts.append(_register(key, ref))
                            i = end
                            continue
                        ref = Ref(alias_name)[outer_idx]
                    key = f'__expr__{raw}'
                    if outer_idx is not None:
                        key += f'[{outer_idx}]'
                    out_parts.append(_register(key, ref))
                    i = end
                else:
                    out_parts.append(text[i])
                    i += 1
            return ''.join(out_parts)

        result = _scan_quoted(result)

        return result, refs

    # ====================================================================
    # FOR expression parser
    # ====================================================================

    @staticmethod
    def _parse_for_expr(expr: str):
        """
        Parse C-style ``"i=0; i<10; i+=1"`` into
        ``(start, cond_str, limit, op_str, step)``.
        
        Returns raw tokens (strings or floats), which should then be
        resolved via ``_resolve_ref()`` by the caller.
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
            """Convert token to float or leave as string (for _resolve_ref)."""
            # Remove outer quotes if present
            if tok.startswith('"') and tok.endswith('"'):
                tok = tok[1:-1]
            elif tok.startswith("'") and tok.endswith("'"):
                tok = tok[1:-1]
            # Try numeric conversion
            try:
                return float(tok)
            except ValueError:
                return tok  # return as string for _resolve_ref

        return _to_val(start_tok), cond_str, _to_val(limit_tok), op_str, _to_val(step_tok)

    # ====================================================================
    # Block factory helpers
    # ====================================================================

    def _add_block(self, block_cls, *, alias=None, idx=None,
                   resolve: tuple = (), **kwargs):
        """Generic block creation: auto-idx → resolve refs → instantiate → register."""
        if idx is None:
            idx = self._idx()
        for key in resolve:
            if key in kwargs:
                kwargs[key] = self._resolve_ref(kwargs[key])
        block = block_cls(idx=idx, ctx=self.blocks_ctx, **kwargs)
        return self._register_block(block, alias=alias)

    def _add_expr_block(self, block_cls, expression, connections, *,
                        en=None, alias=None, idx=None):
        """Creation helper for expression-based blocks (Math, Logic)."""
        en = self._resolve_ref(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        return self._add_block(
            block_cls, alias=alias, idx=idx,
            expression=expression,
            connections=connections or None,
            en=en,
        )

    # ====================================================================
    # Block factory methods  (no idx required — auto-assigned)
    # ====================================================================

    def add_math(self, expression: str, connections=None, en=None,
                 idx: Optional[int] = None,
                 alias: Optional[str] = None) -> 'BlockMath':
        from BlockMath import BlockMath
        return self._add_expr_block(BlockMath, expression, connections,
                                    en=en, alias=alias, idx=idx)

    def add_logic(self, expression: str, connections=None, en=None,
                  idx: Optional[int] = None,
                  alias: Optional[str] = None) -> 'BlockLogic':
        from BlockLogic import BlockLogic
        return self._add_expr_block(BlockLogic, expression, connections,
                                    en=en, alias=alias, idx=idx)

    def add_set(self, target=None, value=None, en=None,
                idx: Optional[int] = None,
                alias: Optional[str] = None) -> 'BlockSet':
        from BlockSet import BlockSet
        return self._add_block(BlockSet, alias=alias, idx=idx,
                               resolve=('target', 'value', 'en'),
                               target=target, value=value, en=en)

    def add_for(self, expr: str = None,
                start=0.0, limit=10.0, step=1.0,
                condition=None, operator=None, en=None,
                chain_len: int = 0,
                idx: Optional[int] = None,
                alias: Optional[str] = None) -> 'BlockFor':
        from BlockFor import BlockFor, _resolve_condition, _resolve_operator
        en = self._resolve_ref(en)
        if expr is not None:
            start, condition, limit, operator, step = self._parse_for_expr(expr)
            start = self._resolve_ref(start)
            limit = self._resolve_ref(limit)
            step  = self._resolve_ref(step)
        return self._add_block(
            BlockFor, alias=alias, idx=idx,
            chain_len=chain_len, start=start, limit=limit, step=step,
            condition=_resolve_condition(condition),
            operator=_resolve_operator(operator), en=en,
        )

    def add_clock(self, period_ms=1000.0, width_ms=500.0, en=None,
                  idx: Optional[int] = None,
                  alias: Optional[str] = None) -> 'BlockClock':
        from BlockClock import BlockClock
        return self._add_block(BlockClock, alias=alias, idx=idx,
                               resolve=('en', 'period_ms', 'width_ms'),
                               period_ms=period_ms, width_ms=width_ms, en=en)

    def add_timer(self, timer_type=None, pt=1000, en=None, rst=None,
                  idx: Optional[int] = None,
                  alias: Optional[str] = None) -> 'BlockTimer':
        from BlockTimer import BlockTimer, TimerType
        if timer_type is None:
            timer_type = TimerType.TON
        return self._add_block(BlockTimer, alias=alias, idx=idx,
                               resolve=('en', 'rst', 'pt'),
                               timer_type=timer_type, pt=pt, en=en, rst=rst)

    def add_counter(self, cu=None, cd=None, reset=None,
                    step=1.0, limit_max=100.0, limit_min=0.0,
                    start_val=0.0, mode=None,
                    idx: Optional[int] = None,
                    alias: Optional[str] = None) -> 'BlockCounter':
        from BlockCounter import BlockCounter, CounterMode
        if mode is None:
            mode = CounterMode.ON_RISING
        return self._add_block(
            BlockCounter, alias=alias, idx=idx,
            resolve=('cu', 'cd', 'reset', 'step', 'limit_max', 'limit_min'),
            cu=cu, cd=cd, reset=reset,
            step=step, limit_max=limit_max, limit_min=limit_min,
            start_val=start_val, mode=mode,
        )

    def add_in_selector(self, selector=None, options=None, en=None,
                        idx: Optional[int] = None,
                        alias: Optional[str] = None) -> 'BlockInSelector':
        from BlockInSelector import BlockInSelector
        if options and isinstance(options[0], str):
            options = [Ref(o) if isinstance(o, str) else o for o in options]
        return self._add_block(BlockInSelector, alias=alias, idx=idx,
                               resolve=('selector', 'en'),
                               selector=selector, options=options or [], en=en)

    def add_latch(self, set=None, reset=None, en=None, latch_type=None,
                  idx: Optional[int] = None,
                  alias: Optional[str] = None) -> 'BlockLatch':
        from BlockLatch import BlockLatch, BlockLatchCfg
        if latch_type is None:
            latch_type = BlockLatchCfg.LATCH_SR
        return self._add_block(BlockLatch, alias=alias, idx=idx,
                               resolve=('set', 'reset', 'en'),
                               set=set, reset=reset, en=en,
                               latch_type=latch_type)

    def add_q_selector(self, selector=None, output_count=1, en=None,
                       idx: Optional[int] = None,
                       alias: Optional[str] = None) -> 'BlockQSelector':
        from BlockQSelector import BlockQSelector
        return self._add_block(BlockQSelector, alias=alias, idx=idx,
                               resolve=('selector', 'en'),
                               selector=selector, output_count=output_count,
                               en=en)

    # ====================================================================
    # Block management
    # ====================================================================

    def _register_block(self, block: Block, alias: Optional[str] = None) -> Block:
        """Register a block, optionally storing a named alias.

        When *alias* is provided, every block output ``{idx}_q_N``
        gets a secondary alias ``{alias}_q_N`` in the blocks context
        so that ``Ref("calc_q_0")`` resolves just like ``Ref("1000_q_0")``.
        The ``BlockOutputProxy`` is also updated so that ``block.out[N]``
        returns ``Ref("{alias}_q_N")``.
        """
        if block.idx in self.blocks:
            raise ValueError(f"Block with index {block.idx} already exists")
        if alias is not None:
            if alias in self._block_aliases:
                raise ValueError(f"Block alias '{alias}' already in use")
            self._block_aliases[alias] = block

            # Set the user alias on the output proxy so that
            # block.out[N] returns Ref("alias_q_N") instead of
            # Ref("{idx}_q_N").
            block.out.user_alias = alias

            # Register secondary aliases in blocks_ctx.alias_map:
            # "calc_q_0" → same (type, idx) as "1000_q_0", etc.
            for q_num in range(len(block.q_conn)):
                numeric_alias = f"{block.idx}_q_{q_num}"
                named_alias   = f"{alias}_q_{q_num}"
                if numeric_alias in self.blocks_ctx.alias_map:
                    self.blocks_ctx.alias_map[named_alias] = (
                        self.blocks_ctx.alias_map[numeric_alias]
                    )

        self.blocks[block.idx] = block
        return block

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
                 verbose: bool = True,
                 subscriptions=None):
        """
        Sort blocks → reindex → write hex dump to *filename*.

        :param filename:       Output file path.
        :param raw:            If True, write without comments (for BLE send).
        :param sort:           If True (default), run topological sort + reindex.
        :param verbose:        Print summary to stdout.
        :param subscriptions:  SubscriptionBuilder instance – if given, subscription
                               packets are appended after block data and before loop control.
        """
        from FullDump import FullDump

        if sort:
            self._sort()

        dump = FullDump(self, subscriptions=subscriptions)

        with open(filename, "w") as f:
            if raw:
                dump.write_raw(f)
            else:
                dump.write(f)


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

    # ====================================================================
    # Subscription helpers
    # ====================================================================

    def subscribe(self, *targets: Union[str, Ref]) -> 'SubscriptionBuilder':
        """
        Create a SubscriptionBuilder and subscribe to the given targets.

        Each target can be:
          - a string alias     (e.g. ``"temperature"``, ``"gains"``)
          - a Ref object       (e.g. ``ton.out[0]``, ``Ref("counter")``)

        Usage:
            sub = code.subscribe("temperature", "counter", "gains")
            sub = code.subscribe("temp", ton.out[0], clk.out[0])

        You can also chain more calls:
            sub = code.subscribe("temp").add("counter").add(ton.out[0])

        To get raw packets:
            init_pkt, add_pkts = sub.build()

        To append subscription sections into a dump file:
            code.generate("dump.txt", subscriptions=sub)
        """
        from Subscribe import SubscriptionBuilder
        builder = SubscriptionBuilder(self)
        for t in targets:
            builder.add(t)
        return builder

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
