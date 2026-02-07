"""
TestSpace - Simplified wrapper for building emulator programs.

Just add blocks and variables, then call generate() — everything else
(auto-idx, topological sort, chain_len, reindex, file output) is automatic.

Usage::

    from TestSpace import TestSpace
    from Enums import mem_types_t

    ts = TestSpace()

    # Variables
    ts.var(mem_types_t.MEM_F, "speed", data=10.0)
    ts.var(mem_types_t.MEM_F, "result", data=0.0)
    ts.var(mem_types_t.MEM_B, "enable", data=True)

    # Blocks (no idx needed — auto-assigned)
    clk = ts.clock(period_ms=1000, width_ms=500, en="enable")

    loop = ts.for_loop(expr="i=0; i<10; i+=1", en=clk.out[0])

    m = ts.math(expression='"speed" * 2.0', en=loop.out[0])

    ts.set(target="result", value=m.out[1])

    # Generate ready file
    ts.generate("test_dump.txt")
"""

from __future__ import annotations
import io
from typing import List, Optional, Union

from Enums import mem_types_t
from Code import Code
from FullDump import FullDump
from algorithm import sort_and_reindex


# ============================================================================
# Auto-idx counter
# ============================================================================

class _IdxCounter:
    """Simple auto-incrementing counter starting from a high number
    so it doesn't clash with anything.  After sort+reindex the final
    indices will be 0..N-1 anyway."""

    def __init__(self, start: int = 1000):
        self._next = start

    def __call__(self) -> int:
        val = self._next
        self._next += 1
        return val


# ============================================================================
# TestSpace
# ============================================================================

class TestSpace:
    """
    Simplified block-programming environment.

    * No manual ``idx`` — auto-assigned.
    * No manual ``chain_len`` — auto-computed by the sorting algorithm.
    * Call :meth:`generate` to topologically sort, reindex, and write
      the ready hex dump file.
    """

    def __init__(self):
        self._code = Code()
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
        Add a user variable.

        :param var_type: MEM_F, MEM_B, MEM_U8, MEM_U16, MEM_U32, MEM_I32 …
        :param alias: Unique name
        :param data: Initial value (scalar or list for arrays)
        :param dims: Array dimensions (e.g. [5] for 1-D, [3,4] for 2-D)
        """
        self._code.add_variable(var_type, alias, data=data, dims=dims)

    # ====================================================================
    # Blocks — thin wrappers that inject auto-idx
    # ====================================================================

    def math(self, expression: str, connections=None, en=None):
        """Add a MATH block.  See :pymeth:`Code.add_math`."""
        return self._code.add_math(idx=self._idx(),
                                   expression=expression,
                                   connections=connections,
                                   en=en)

    def logic(self, expression: str, connections=None, en=None):
        """Add a LOGIC block.  See :pymeth:`Code.add_logic`."""
        return self._code.add_logic(idx=self._idx(),
                                    expression=expression,
                                    connections=connections,
                                    en=en)

    def set(self, target=None, value=None):
        """Add a SET block.  See :pymeth:`Code.add_set`."""
        return self._code.add_set(idx=self._idx(),
                                  target=target,
                                  value=value)

    def for_loop(self,
                 expr: str = None,
                 start=0.0, limit=10.0, step=1.0,
                 condition=None, operator=None,
                 en=None):
        """
        Add a FOR block.

        ``chain_len`` is **not** required — it will be auto-computed
        during :meth:`generate`.

        :param expr: C-style ``"i=0; i<10; i+=1"``
        """
        return self._code.add_for(idx=self._idx(),
                                  chain_len=0,   # placeholder — auto-computed
                                  expr=expr,
                                  start=start, limit=limit, step=step,
                                  condition=condition, operator=operator,
                                  en=en)

    def clock(self, period_ms=1000.0, width_ms=500.0, en=None):
        """Add a CLOCK block.  See :pymeth:`Code.add_clock`."""
        return self._code.add_clock(idx=self._idx(),
                                    period_ms=period_ms,
                                    width_ms=width_ms,
                                    en=en)

    def timer(self, timer_type=None, pt=1000, en=None, rst=None):
        """Add a TIMER block.  See :pymeth:`Code.add_timer`."""
        return self._code.add_timer(idx=self._idx(),
                                    timer_type=timer_type,
                                    pt=pt, en=en, rst=rst)

    def counter(self, cu=None, cd=None, reset=None,
                step=1.0, limit_max=100.0, limit_min=0.0,
                start_val=0.0, mode=None):
        """Add a COUNTER block.  See :pymeth:`Code.add_counter`."""
        return self._code.add_counter(idx=self._idx(),
                                      cu=cu, cd=cd, reset=reset,
                                      step=step,
                                      limit_max=limit_max,
                                      limit_min=limit_min,
                                      start_val=start_val,
                                      mode=mode)

    def selector(self, selector=None, options=None):
        """Add a SELECTOR block.  See :pymeth:`Code.add_selector`."""
        from MemAcces import Ref
        if isinstance(selector, str):
            selector = Ref(selector)
        if options and isinstance(options[0], str):
            options = [Ref(o) if isinstance(o, str) else o for o in options]
        return self._code.add_selector(idx=self._idx(),
                                       selector=selector,
                                       options=options or [])

    # ====================================================================
    # Generation pipeline
    # ====================================================================

    def _sort(self):
        """Topological sort + reindex + auto chain_len.  Mutates in-place."""
        sorted_blocks = sort_and_reindex(self._code.blocks)
        # Rebuild the Code.blocks dict with new indices as keys
        self._code.blocks = {b.idx: b for b in sorted_blocks}

    def generate(self,
                 filename: str = "test_dump.txt",
                 raw: bool = False,
                 sort: bool = True,
                 verbose: bool = True):
        """
        Sort blocks, reindex, and write the hex dump to *filename*.

        :param filename: Output file path.
        :param raw: If True, write without comments (for direct BLE send).
        :param sort: If True (default), run topological sort + reindex.
        :param verbose: Print summary to stdout.
        """
        if sort:
            self._sort()

        dump = FullDump(self._code)

        with open(filename, "w") as f:
            if raw:
                dump.write_raw(f)
            else:
                dump.write(f)

        if verbose:
            self._print_summary(filename)

    def generate_raw_string(self, sort: bool = True) -> str:
        """Return raw hex dump as a string (useful for BLE transmission)."""
        if sort:
            self._sort()
        dump = FullDump(self._code)
        buf = io.StringIO()
        dump.write_raw(buf)
        return buf.getvalue()

    # ====================================================================
    # Helpers
    # ====================================================================

    def _print_summary(self, filename: str):
        """Print a nice summary after generation."""
        blocks = self._code.get_blocks_sorted()
        print(f"\n{'='*60}")
        print(f"  TestSpace — generated {filename}")
        print(f"{'='*60}")
        print(f"  Variables : {len(self._code.user_ctx.alias_map)}")
        print(f"  Blocks    : {len(blocks)}")
        print(f"  Execution order:")
        for b in blocks:
            extra = ""
            if hasattr(b, 'chain_len'):
                extra = f"  chain_len={b.chain_len}"
            print(f"    [{b.idx}] {b.block_type.name}{extra}")
        print(f"{'='*60}\n")

    @property
    def code(self) -> Code:
        """Access the underlying Code object (for advanced use)."""
        return self._code


# ============================================================================
# DEMO
# ============================================================================

if __name__ == "__main__":
    from MemAcces import Ref

    print("=" * 60)
    print("TestSpace Demo")
    print("=" * 60)

    ts = TestSpace()

    # --- Variables ---
    ts.var(mem_types_t.MEM_F, "speed", data=3.3)
    ts.var(mem_types_t.MEM_F, "factor", data=2.0)
    ts.var(mem_types_t.MEM_F, "result", data=0.0)
    ts.var(mem_types_t.MEM_B, "enable", data=True)
    ts.var(mem_types_t.MEM_F, "threshold", data=50.0)

    # --- Blocks (added in ANY order, no idx needed) ---
    # CLOCK is a root
    c = ts.clock(period_ms=5000, width_ms=1000, en="enable")

    # Standalone LOGIC (no block deps — root)
    l = ts.logic(expression='"speed" > "threshold"',
                 en="enable")

    # MATH depends on clock enable
    m = ts.math(expression='"speed" * "factor"',
                en=c.out[0])

    # SET depends on math output
    s = ts.set(target="result", value=m.out[1])

    # --- Generate (auto-sort, reindex, write) ---
    ts.generate("test_dump.txt")

    print("\n" + "=" * 60)
    print("TestSpace Demo 2 — FOR loop with transitive deps")
    print("=" * 60)

    ts2 = TestSpace()

    ts2.var(mem_types_t.MEM_F, "input_a", data=1.1)
    ts2.var(mem_types_t.MEM_F, "input_b", data=[1, 2, 3, 4, 5], dims=[5])
    ts2.var(mem_types_t.MEM_F, "output", data=0.0)
    ts2.var(mem_types_t.MEM_B, "enable", data=True)

    # Blocks — add in ANY order, reference outputs freely
    clk = ts2.clock(period_ms=5000, width_ms=1000, en="enable")
    loop = ts2.for_loop(expr="i=0; i<10; i+=1", en=clk.out[0])
    m2 = ts2.math(expression='("input_a" * "input_b"[2]) + "output"',
                  en=loop.out[0])
    ts2.set(target="output", value=m2.out[1])

    ts2.generate("test_dump.txt")
