from Code import Code
from Enums import mem_types_t

code = Code()

# Variables
code.var(mem_types_t.MEM_F, "speed",  data=3.3)
code.var(mem_types_t.MEM_F, "result", data=1)
code.var(mem_types_t.MEM_B, "enable", data=True)
code.var(mem_types_t.MEM_F, "one",    data=1)

# Blocks — no idx, no chain_len, any order
clk   = code.add_clock(period_ms=5000, width_ms=1000, en="enable")
loop  = code.add_for(expr="i=0; i<10; i+=1", en=clk.out[0])
m     = code.add_math(expression='"speed" * 2.0 * "result"', en=loop.out[0])
code.add_set(target="result", value=m.out[1])
rst   = code.add_set(target="result", value="one")
logic = code.add_logic(expression='"result" > 10.0', en=m.out[1])
math3 = code.add_math(expression='"result" * 3.0', en=logic.out[0])
for2  = code.add_for(expr="j=0; j<5; j+=1", en=logic.out[0])
m2    = code.add_math(expression='"result" + 5.0', en=for2.out[0])
code.add_set(target="result", value=m2.out[1])

# One call → sort, reindex, auto chain_len, write file
code.generate("test_dump.txt")
