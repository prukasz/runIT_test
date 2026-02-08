from Code import Code
from Enums import mem_types_t
from MemAcces import Ref

code = Code()

# ── Variables ──────────────────────────────────────────────
code.var(mem_types_t.MEM_F, "speed",   data=2.0)
code.var(mem_types_t.MEM_F, "result",  data=1)
code.var(mem_types_t.MEM_B, "enable",  data=True)
code.var(mem_types_t.MEM_F, "one",     data=1)

# User-created array: 5-element float array
code.var(mem_types_t.MEM_F, "gains", data=[1,2,3,4,5,6,7,8,9,10], dims=[10])

# ── Blocks ─────────────────────────────────────────────────

clk  = code.add_clock(period_ms=5000, width_ms=1000, en="enable", alias="clk")

loop = code.add_for(expr="i=0; i<11; i+=1", en="clk[0]", alias="loop")


m = code.add_math(expression='"speed" * "result"',
                     en="loop[0]", alias="calc")

# Set result from block output via alias
code.add_set(target="result", value="calc[1]")

m2 = code.add_math(expression='"result" + "gains["loop[1]"]"', en="loop[0]", alias="calc2")

rst  = code.add_set(target="result", value="one")

# ── Generate ───────────────────────────────────────────────
code.generate("test_dump.txt")
