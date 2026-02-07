import struct
from typing import List, Dict, Optional, Type
from dataclasses import dataclass, field

from Enums import packet_header_t
from Mem import mem_context_t
from MemAcces import AccessManager
from Block import Block

# ============================================================================
# CODE STORAGE - Manages all blocks in execution order
# ============================================================================

# Context IDs
CTX_USER = 0    # Context 0: User-created variables
CTX_BLOCKS = 1  # Context 1: Block outputs (hidden from API)

@dataclass
class Code:
    """
    Code storage for emulator blocks.
    
    Manages two contexts:
    - Context 0 (user_ctx): For user-created variables, exposed via API
    - Context 1 (blocks_ctx): For block outputs, hidden from API
    
    Generates packets in correct order for C parser:
    1. All block headers (cfg)
    2. All block inputs
    3. All block outputs  
    4. All block data (custom per block type)
    
    Usage:
        code = Code()
        
        # Add user variables to context 0
        code.add_variable(mem_types_t.MEM_F, "speed", data=10.0)
        code.add_variable(mem_types_t.MEM_F, "angle", data=45.0)
        
        # Create blocks - outputs auto-created in context 1
        math = code.add_math_block(idx=0, expression="in_1 + in_2", ...)
        
        # Chain blocks using .out proxy
        set_block = code.add_block(Block, idx=1, ...)
        set_block.add_inputs([math.out[1]])
        
        # Generate all packets
        packets = code.generate_packets()
    """
    
    user_ctx: mem_context_t    # Context 0: user variables
    blocks_ctx: mem_context_t  # Context 1: block outputs (hidden)
    blocks: Dict[int, Block] = field(default_factory=dict)
    _manager: AccessManager = field(default=None)
    
    def __init__(self):
        self.user_ctx = mem_context_t(ctx_id=CTX_USER)
        self.blocks_ctx = mem_context_t(ctx_id=CTX_BLOCKS)
        self.blocks = {}
        
        # Setup AccessManager
        AccessManager.reset()
        self._manager = AccessManager.get_instance()
        self._manager.register_context(self.user_ctx)
        self._manager.register_context(self.blocks_ctx)
    
    # ========================================================================
    # User Variables API (Context 0)
    # ========================================================================
    
    def add_variable(self, 
                     var_type: 'mem_types_t', 
                     alias: str, 
                     data=None, 
                     dims: Optional[List[int]] = None):
        """
        Add a user variable to context 0.
        
        :param var_type: Memory type (MEM_F, MEM_U16, etc.)
        :param alias: Unique name for the variable
        :param data: Initial value
        :param dims: Dimensions for arrays
        """
        from Enums import mem_types_t
        self.user_ctx.add(type=var_type, alias=alias, data=data, dims=dims)
    
    def ref(self, alias: str) -> 'Ref':
        """Get a Ref to a user variable by alias."""
        from MemAcces import Ref
        return Ref(alias)
    
    # ========================================================================
    # Internal Helpers — resolve aliases to Refs
    # ========================================================================

    @staticmethod
    def _resolve_ref(value):
        """
        If *value* is already a Ref  → return as-is.
        If it is a str              → wrap in Ref(value).
        If it is int / float / None → return as-is (constant or absent).
        """
        from MemAcces import Ref
        if value is None or isinstance(value, (int, float, Ref)):
            return value
        if isinstance(value, str):
            return Ref(value)
        raise TypeError(f"Cannot resolve {type(value)} to Ref")

    @staticmethod
    def _extract_refs_from_expr(expression: str):
        """
        Scan *expression* for quoted aliases  "alias"  or  block output
        tokens  blk_N.out[M]  and replace each unique one with in_N
        (starting at in_1).  Returns (rewritten_expr, connections_list).

        Supported token forms inside the expression string:
            "my_var"          → Ref("my_var")
            "my_arr"[2]       → Ref("my_arr")[2]
            blk_3.out[1]      → block-output Ref  (alias = "3_q_1")

        Anything already written as in_N is left untouched.
        Bare numbers are left untouched (they become constants in the parser).
        """
        import re
        from MemAcces import Ref

        refs: list = []          # ordered unique refs
        alias_to_idx: dict = {}  # alias_key → in_N index (1-based)

        def _next_idx():
            return len(refs) + 1

        def _register(alias_key: str, ref_obj):
            """Register a ref and return its in_N token."""
            if alias_key not in alias_to_idx:
                idx = _next_idx()
                alias_to_idx[alias_key] = idx
                refs.append(ref_obj)
            return f'in_{alias_to_idx[alias_key]}'

        result = expression

        # 1) block output tokens:  blk_<N>.out[<M>]  (no quotes)
        blk_pat = re.compile(r'blk_(\d+)\.out\[(\d+)\]')
        def _replace_blk(m):
            bidx, oidx = int(m.group(1)), int(m.group(2))
            key = f'{bidx}_q_{oidx}'
            return _register(key, Ref(key))
        result = blk_pat.sub(_replace_blk, result)

        # 2) quoted aliases with optional array index:  "alias"[idx]
        #    idx can be a number or a "ref" (will recurse)
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

    # ========================================================================
    # Block Creation (Factory Methods)
    # ========================================================================
    
    def add_math(self,
                 idx: int,
                 expression: str,
                 connections: Optional[List] = None,
                 en=None) -> 'BlockMath':
        """
        Create and add a Math block.

        Two calling styles:

        **Classic (in_N)**::

            code.add_math(idx=1,
                          expression="in_1 * 3.14 + in_2",
                          connections=[Ref("speed"), Ref("offset")])

        **Alias (new)**::

            code.add_math(idx=1,
                          expression='"speed" * 3.14 + "offset"')

        Quoted aliases are auto-resolved to Ref objects and mapped to in_N.
        Block outputs can be written as  blk_3.out[1].
        If *connections* is provided the expression is used as-is (classic mode).
        """
        from BlockMath import BlockMath
        en = self._resolve_ref(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        block = BlockMath(
            idx=idx,
            ctx=self.blocks_ctx,
            expression=expression,
            connections=connections if connections else None,
            en=en
        )
        return self.add_block(block)
    
    def create_block(self, idx: int, block_type: block_types_t) -> Block:
        """
        Create a generic block with correct context.
        
        :param idx: Block index
        :param block_type: Type of block (blck_types_t enum)
        :return: The created Block instance (call add_block to register)
        
        Usage:
            block = code.create_block(1, blck_types_t.BLOCK_SET)
            block.add_inputs([...])
            block.add_output(mem_types_t.MEM_F)
            code.add_block(block)
        """
        return Block(idx=idx, block_type=block_type, ctx=self.blocks_ctx)
    
    def add_set(self, idx: int, target=None, value=None) -> 'BlockSet':
        """
        Create and add a SET block.

        *target* and *value* accept a Ref **or** a plain string alias
        (auto-wrapped in Ref)::

            code.add_set(idx=5, target="motor_speed", value="calculated")
            code.add_set(idx=5, target=Ref("motor_speed"), value=math.out[1])
        """
        from BlockSet import BlockSet
        target = self._resolve_ref(target)
        value  = self._resolve_ref(value)
        block = BlockSet(idx=idx, ctx=self.blocks_ctx, target=target, value=value)
        return self.add_block(block)
    
    def add_timer(self,
                  idx: int,
                  timer_type=None,
                  pt=1000,
                  en=None,
                  rst=None) -> 'BlockTimer':
        """
        Create and add a Timer block.
        
        :param idx: Block index
        :param timer_type: Timer mode (TON, TOF, TP, etc.)
        :param pt: Preset time in ms (constant or Ref)
        :param en: Enable input Ref
        :param rst: Reset input Ref
        :return: The created BlockTimer instance
        """
        from BlockTimer import BlockTimer, TimerType
        if timer_type is None:
            timer_type = TimerType.TON
        en  = self._resolve_ref(en)
        rst = self._resolve_ref(rst)
        pt  = self._resolve_ref(pt)
        block = BlockTimer(idx=idx, ctx=self.blocks_ctx, timer_type=timer_type, pt=pt, en=en, rst=rst)
        return self.add_block(block)
    
    def add_counter(self,
                    idx: int,
                    cu=None, cd=None, reset=None,
                    step=1.0, limit_max=100.0, limit_min=0.0,
                    start_val=0.0, mode=None) -> 'BlockCounter':
        """
        Create and add a Counter block.
        
        :param idx: Block index
        :param cu: Count Up input Ref
        :param cd: Count Down input Ref
        :param reset: Reset input Ref
        :param step: Step value (constant or Ref)
        :param limit_max: Maximum limit (constant or Ref)
        :param limit_min: Minimum limit (constant or Ref)
        :param start_val: Initial counter value
        :param mode: Counter mode (ON_RISING or WHEN_ACTIVE)
        :return: The created BlockCounter instance
        """
        from BlockCounter import BlockCounter, CounterMode
        if mode is None:
            mode = CounterMode.ON_RISING
        cu    = self._resolve_ref(cu)
        cd    = self._resolve_ref(cd)
        reset = self._resolve_ref(reset)
        step  = self._resolve_ref(step)
        limit_max = self._resolve_ref(limit_max)
        limit_min = self._resolve_ref(limit_min)
        block = BlockCounter(
            idx=idx, ctx=self.blocks_ctx,
            cu=cu, cd=cd, reset=reset,
            step=step, limit_max=limit_max, limit_min=limit_min,
            start_val=start_val, mode=mode
        )
        return self.add_block(block)
    
    def add_clock(self,
                  idx: int,
                  period_ms=1000.0,
                  width_ms=500.0,
                  en=None) -> 'BlockClock':
        """
        Create and add a Clock/PWM block.
        
        :param idx: Block index
        :param period_ms: Period in ms (constant or Ref)
        :param width_ms: Pulse width in ms (constant or Ref)
        :param en: Enable input Ref
        :return: The created BlockClock instance
        """
        from BlockClock import BlockClock
        en = self._resolve_ref(en)
        period_ms = self._resolve_ref(period_ms)
        width_ms  = self._resolve_ref(width_ms)
        block = BlockClock(idx=idx, ctx=self.blocks_ctx, period_ms=period_ms, width_ms=width_ms, en=en)
        return self.add_block(block)
    
    def add_logic(self,
                  idx: int,
                  expression: str,
                  connections=None,
                  en=None) -> 'BlockLogic':
        """
        Create and add a Logic block.

        Two calling styles:

        **Classic (in_N)**::

            code.add_logic(idx=1,
                           expression="(in_1 > 50) && (in_2 < 100)",
                           connections=[Ref("temp"), Ref("press")])

        **Alias (new)**::

            code.add_logic(idx=1,
                           expression='("temp" > 50) && ("press" < 100)')

        Block outputs: ``blk_3.out[1] > 10``.
        If *connections* is provided the expression is used as-is.
        """
        from BlockLogic import BlockLogic
        en = self._resolve_ref(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        block = BlockLogic(idx=idx, ctx=self.blocks_ctx, expression=expression,
                           connections=connections if connections else None, en=en)
        return self.add_block(block)
    
    @staticmethod
    def _parse_for_expr(expr: str):
        """
        Parse a C-style for expression into (start, condition_str, limit, operator_str, step).

        Accepted formats (semicolon-separated, iterator name is ignored)::

            "i = 0; i < 10; i += 1"
            "i = 0; i < threshold; i += 1"

        *start*, *limit*, *step* are returned as ``float`` when numeric,
        or as ``Ref(alias)`` when they look like a variable name.
        """
        import re
        from MemAcces import Ref

        parts = [p.strip() for p in expr.split(';')]
        if len(parts) != 3:
            raise ValueError(f"For expression must have 3 semicolon-separated parts, got {len(parts)}: '{expr}'")

        # --- init:  <var> = <value> ---
        init_m = re.match(r'(\w+)\s*=\s*(.+)', parts[0])
        if not init_m:
            raise ValueError(f"Cannot parse init part: '{parts[0]}'")
        start_tok = init_m.group(2).strip()

        # --- condition:  <var> <op> <value> ---
        cond_m = re.match(r'\w+\s*(<=|>=|<|>)\s*(.+)', parts[1])
        if not cond_m:
            raise ValueError(f"Cannot parse condition part: '{parts[1]}'")
        cond_str = cond_m.group(1)
        limit_tok = cond_m.group(2).strip()

        # --- step:  <var> <op>= <value>  OR  <var> = <var> <op> <value> ---
        step_m = re.match(r'\w+\s*([+\-*/])=\s*(.+)', parts[2])
        if not step_m:
            # try  i = i + 1  form
            step_m2 = re.match(r'\w+\s*=\s*\w+\s*([+\-*/])\s*(.+)', parts[2])
            if not step_m2:
                raise ValueError(f"Cannot parse step part: '{parts[2]}'")
            step_m = step_m2
        op_str = step_m.group(1)
        step_tok = step_m.group(2).strip()

        def _to_val(tok):
            """Number → float, else → Ref."""
            # strip optional quotes
            tok = tok.strip('"').strip("'")
            try:
                return float(tok)
            except ValueError:
                return Ref(tok)

        return _to_val(start_tok), cond_str, _to_val(limit_tok), op_str, _to_val(step_tok)

    def add_for(self,
                idx: int,
                chain_len: int,
                expr: str = None,
                start=0.0, limit=10.0, step=1.0,
                condition=None, operator=None,
                en=None) -> 'BlockFor':
        """
        Create and add a FOR loop block.

        Two calling styles:

        **C-style expression (new)**::

            code.add_for(idx=1, chain_len=2,
                         expr="i = 0; i < 10; i += 1")

        Variable names in the expression become Ref automatically::

            code.add_for(idx=1, chain_len=2,
                         expr="i = 0; i < threshold; i += 1")

        **Classic keywords (still works)**::

            code.add_for(idx=1, chain_len=2,
                         start=0, limit=10, step=1,
                         condition="<", operator="+")

        :param expr: C-style for expression (takes priority over keywords)
        """
        from BlockFor import BlockFor, _resolve_condition, _resolve_operator
        en = self._resolve_ref(en)

        if expr is not None:
            start, condition, limit, operator, step = self._parse_for_expr(expr)

        condition = _resolve_condition(condition)
        operator  = _resolve_operator(operator)
        block = BlockFor(
            idx=idx, ctx=self.blocks_ctx, chain_len=chain_len,
            start=start, limit=limit, step=step,
            condition=condition, operator=operator, en=en
        )
        return self.add_block(block)

    def add_selector(self,
                     idx: int,
                     selector: 'Ref',
                     options: List['Ref']) -> 'BlockSelector':
        """
        Create and add a Selector block.
        
        :param idx: Block index
        :param selector: Ref to selector variable (U8)
        :param options: List of Refs to choose from (max 16)
        :return: The created BlockSelector instance
        """
        from BlockSelector import BlockSelector
        block = BlockSelector(idx=idx, ctx=self.blocks_ctx, selector=selector, options=options)
        return self.add_block(block)
    
    # ========================================================================
    # Block Management
    # ========================================================================
    
    def add_block(self, block: Block) -> Block:
        """
        Add a block to the code storage.
        
        :param block: Block instance to add
        :return: The added block
        :raises ValueError: If block index already exists
        """
        if block.idx in self.blocks:
            raise ValueError(f"Block with index {block.idx} already exists")
        self.blocks[block.idx] = block
        return block
    
    def get_block(self, idx: int) -> Optional[Block]:
        """Get block by index."""
        return self.blocks.get(idx)
    
    def get_blocks_sorted(self) -> List[Block]:
        """Get all blocks sorted by index."""
        return [self.blocks[idx] for idx in sorted(self.blocks.keys())]
    
    @property
    def block_count(self) -> int:
        """Number of blocks in code."""
        return len(self.blocks)
    
    def generate_code_cfg_packet(self) -> bytes:
        """
        Generate code configuration packet.
        Format: [header:u8][block_count:u16]
        """
        return struct.pack('<BH',
            packet_header_t.PACKET_H_CODE_CFG,
            self.block_count
        )
    
    def generate_packets(self, manager: Optional[AccessManager] = None) -> List[bytes]:
        """
        Generate all packets for the code in correct order:
        1. Code config packet
        2. All block headers (cfg)
        3. All block inputs
        4. All block outputs
        5. All block data
        
        :param manager: AccessManager instance (uses internal if None)
        :return: List of packets in order
        """
        if manager is None:
            manager = self._manager
        
        packets = []
        blocks_sorted = self.get_blocks_sorted()
        
        # 1. Code config
        packets.append(self.generate_code_cfg_packet())
        
        # 2. All block headers
        for block in blocks_sorted:
            # Add header packet marker
            header = struct.pack('<BH',
                packet_header_t.PACKET_H_BLOCK_HEADER,
                block.idx
            )
            packets.append(header + block.pack_cfg())
        
        # 3. All block inputs
        for block in blocks_sorted:
            packets.extend(block.pack_inputs(manager))
        
        # 4. All block outputs
        for block in blocks_sorted:
            packets.extend(block.pack_outputs(manager))
        
        # 5. All block data (if block has pack_data method)
        for block in blocks_sorted:
            if hasattr(block, 'pack_data'):
                data_packets = block.pack_data()
                if data_packets:
                    packets.extend(data_packets)
        
        return packets
    
    def generate_packets_hex(self, manager: Optional[AccessManager] = None) -> List[str]:
        """Generate packets and return as hex strings."""
        if manager is None:
            manager = self._manager
        return [pkt.hex().upper() for pkt in self.generate_packets(manager)]
    
    def print_packets(self, manager: Optional[AccessManager] = None):
        """Print all packets with labels."""
        if manager is None:
            manager = self._manager
        
        blocks_sorted = self.get_blocks_sorted()
        
        print(f"\n{'='*60}")
        print(f"Code Packets ({self.block_count} blocks)")
        print('='*60)
        
        # Code config
        cfg_pkt = self.generate_code_cfg_packet()
        print(f"\n[CODE_CFG] {cfg_pkt.hex().upper()}")
        
        # Headers
        print(f"\n--- Block Headers ---")
        for block in blocks_sorted:
            header = struct.pack('<BH', packet_header_t.PACKET_H_BLOCK_HEADER, block.idx)
            pkt = header + block.pack_cfg()
            print(f"  [{block.idx}] {block.block_type.name}: {pkt.hex().upper()}")
        
        # Inputs
        print(f"\n--- Block Inputs ---")
        for block in blocks_sorted:
            in_pkts = block.pack_inputs(manager)
            if in_pkts:
                for i, pkt in enumerate(in_pkts):
                    print(f"  [{block.idx}] Input {i}: {pkt.hex().upper()}")
        
        # Outputs
        print(f"\n--- Block Outputs ---")
        for block in blocks_sorted:
            out_pkts = block.pack_outputs(manager)
            if out_pkts:
                for i, pkt in enumerate(out_pkts):
                    print(f"  [{block.idx}] Output {i}: {pkt.hex().upper()}")
        
        # Data
        print(f"\n--- Block Data ---")
        for block in blocks_sorted:
            if hasattr(block, 'pack_data'):
                data_pkts = block.pack_data()
                if data_pkts:
                    for pkt in data_pkts:
                        packet_id = pkt[4] if len(pkt) > 4 else 0
                        print(f"  [{block.idx}] Data(0x{packet_id:02X}): {pkt.hex().upper()}")
        
        print('='*60)
    
    def __repr__(self) -> str:
        return f"Code(blocks={self.block_count}, user_vars={len(self.user_ctx.alias_map)}, block_outputs={len(self.blocks_ctx.alias_map)})"

# ============================================================================
# DEMO / TEST
# ============================================================================

if __name__ == "__main__":
    from Enums import block_types_t, mem_types_t, block_packet_id_t
    from Block import Block
    from BlockMath import BlockMath
    from BlockTimer import TimerType
    from BlockCounter import CounterMode
    from BlockFor import ForCondition, ForOperator  # enums still work, strings also accepted
    from MemAcces import Ref
    
    print("=" * 70)
    print("Code Storage Test - Comprehensive Example")
    print("=" * 70)
    
    # ========================================================================
    # 1. Create Code storage (manages both contexts internally)
    # ========================================================================
    code = Code()
    
    # ========================================================================
    # 2. Add user variables to context 0
    # ========================================================================
    print("\n--- Adding User Variables (Context 0) ---")
    
    # Sensor inputs
    code.add_variable(mem_types_t.MEM_F, "temperature", data=25.0)
    code.add_variable(mem_types_t.MEM_F, "pressure", data=101.3)
    code.add_variable(mem_types_t.MEM_F, "setpoint", data=50.0)
    
    # Control signals
    code.add_variable(mem_types_t.MEM_B, "start_btn", data=False)
    code.add_variable(mem_types_t.MEM_B, "stop_btn", data=False)
    code.add_variable(mem_types_t.MEM_B, "reset_btn", data=False)
    
    # Mode selector
    code.add_variable(mem_types_t.MEM_U8, "mode", data=0)
    
    # Output targets
    code.add_variable(mem_types_t.MEM_F, "output_low", data=0.0)
    code.add_variable(mem_types_t.MEM_F, "output_med", data=50.0)
    code.add_variable(mem_types_t.MEM_F, "output_high", data=100.0)
    code.add_variable(mem_types_t.MEM_F, "motor_speed", data=0.0)
    
    print(f"User variables ({len(code.user_ctx.alias_map)}): {list(code.user_ctx.alias_map.keys())}")
    
    # ========================================================================
    # 3. Create blocks using factory methods
    # ========================================================================
    print("\n--- Creating Blocks (Outputs in Context 1) ---")
    
    # Block 0: Math - calculate error (setpoint - temperature)
    math_block = code.add_math(
        idx=0,
        expression='"setpoint" - "temperature"',
        en="start_btn"
    )
    print(f"[0] {math_block}")
    
    # Block 1: Logic - check if temperature is in range
    logic_block = code.add_logic(
        idx=1,
        expression='("temperature" > 20) && ("temperature" < 80)',
        en="start_btn"
    )
    print(f"[1] {logic_block}")
    
    # Block 2: Timer - delay before action (TON 2 seconds)
    timer_block = code.add_timer(
        idx=2,
        timer_type=TimerType.TON,
        pt=2000,
        en=logic_block.out[1],  # Use logic result as enable
        rst="reset_btn"
    )
    print(f"[2] {timer_block}")
    
    # Block 3: Counter - count cycles
    counter_block = code.add_counter(
        idx=3,
        cu=timer_block.out[0],  # Count on timer output
        cd=None,
        reset="reset_btn",
        step=1.0,
        limit_max=100.0,
        limit_min=0.0,
        start_val=0.0,
        mode=CounterMode.ON_RISING
    )
    print(f"[3] {counter_block}")
    
    # Block 4: Clock - generate PWM signal (1Hz, 50% duty)
    clock_block = code.add_clock(
        idx=4,
        period_ms=1000.0,
        width_ms=500.0,
        en="start_btn"
    )
    print(f"[4] {clock_block}")
    
    # Block 5: Selector - select output based on mode
    selector_block = code.add_selector(
        idx=5,
        selector=code.ref("mode"),
        options=[
            code.ref("output_low"),
            code.ref("output_med"),
            code.ref("output_high")
        ]
    )
    print(f"[5] {selector_block}")
    
    # Block 6: Set - assign calculated value to motor_speed
    set_block = code.add_set(
        idx=6,
        target="motor_speed",
        value=math_block.out[1]
    )
    print(f"[6] {set_block}")
    
    # Block 7: For loop - iterate 5 times
    for_block = code.add_for(
        idx=7,
        chain_len=2,
        expr="i = 0; i < 5; i += 1",
        en="start_btn"
    )
    print(f"[7] {for_block}")
    
    # ========================================================================
    # 4. Summary
    # ========================================================================
    print(f"\nBlock output aliases (Context 1): {list(code.blocks_ctx.alias_map.keys())}")
    print(f"\n{code}")
    
    # ========================================================================
    # 5. Print all packets with annotations
    # ========================================================================
    code.print_packets()
    
    # ========================================================================
    # 6. Raw packet list for sending to device
    # ========================================================================
    print("\n--- Raw Packet List (for transmission) ---")
    all_packets = code.generate_packets()
    print(f"Total packets: {len(all_packets)}")
    
    # Group by type
    cfg_count = sum(1 for p in all_packets if p[0] == 0xAA)
    header_count = sum(1 for p in all_packets if p[0] == 0xB0)
    input_count = sum(1 for p in all_packets if p[0] == 0xB1)
    output_count = sum(1 for p in all_packets if p[0] == 0xB2)
    data_count = sum(1 for p in all_packets if p[0] == 0xBA)
    
    print(f"  Config packets: {cfg_count}")
    print(f"  Header packets: {header_count}")
    print(f"  Input packets:  {input_count}")
    print(f"  Output packets: {output_count}")
    print(f"  Data packets:   {data_count}")
    
    print("\n--- Packet Details ---")
    for i, pkt in enumerate(all_packets):
        header = pkt[0]
        if header == 0xAA:
            ptype = "CODE_CFG"
        elif header == 0xB0:
            ptype = "BLK_HDR"
        elif header == 0xB1:
            ptype = "BLK_IN"
        elif header == 0xB2:
            ptype = "BLK_OUT"
        elif header == 0xBA:
            packet_id = pkt[4] if len(pkt) > 4 else 0
            ptype = f"BLK_DATA(0x{packet_id:02X})"
        else:
            ptype = f"UNKNOWN(0x{header:02X})"
        
        print(f"  {i:2d}: [{ptype:16s}] {pkt.hex().upper()}")
    
    print("\n" + "=" * 70)
    print("Packet ID Reference (block_packet_id_t):")
    print(f"  PKT_CONSTANTS    = 0x{block_packet_id_t.PKT_CONSTANTS:02X} (constants: cnt + floats)")
    print(f"  PKT_CFG          = 0x{block_packet_id_t.PKT_CFG:02X} (block-specific configuration)")
    print(f"  PKT_INSTRUCTIONS = 0x{block_packet_id_t.PKT_INSTRUCTIONS:02X} (Math/Logic instructions)")
    print(f"  PKT_OPTIONS_BASE = 0x{block_packet_id_t.PKT_OPTIONS_BASE:02X}+ (Selector options)")
    print("=" * 70)


