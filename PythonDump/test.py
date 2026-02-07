from TestSpace import TestSpace
from Enums import mem_types_t

ts = TestSpace()

# Variables
ts.var(mem_types_t.MEM_F, "speed", data=3.3)
ts.var(mem_types_t.MEM_F, "result", data=1)
ts.var(mem_types_t.MEM_B, "enable", data=True)
ts.var(mem_types_t.MEM_F, "1", data=1)

# Blocks — no idx, no chain_len, any order
clk = ts.clock(period_ms=5000, width_ms=1000, en="enable")
loop = ts.for_loop(expr="i=0; i<10; i+=1", en=clk.out[0])
m = ts.math(expression='"speed" * 2.0 * "result"', en=loop.out[0])
ts.set(target="result", value=m.out[1])
rst = ts.set(target="result", value="1")

# One call → sort, reindex, auto chain_len, write file
ts.generate("test_dump.txt")