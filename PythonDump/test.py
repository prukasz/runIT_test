from Code import Code
from Enums import mem_types_t
from MemAcces import Ref

code = Code()

# ── Variables ──────────────────────────────────────────────
code.var(mem_types_t.MEM_F, "selector",   data=0)
code.var(mem_types_t.MEM_I16, "zero",  data=0)
code.var(mem_types_t.MEM_F, "one",     data=1)
code.var(mem_types_t.MEM_U8, "two",    data=2)
code.var(mem_types_t.MEM_B, "enable", True)

# User-created array: 10-element float array
code.var(mem_types_t.MEM_F, "gains", data=[1,2,3,4,5,6,7,8,9,10], dims=[10])

# ── Blocks ─────────────────────────────────────────────────

clk  = code.add_clock(period_ms=5000, width_ms=1000, en="enable", alias="clk")

# Test IN_SELECTOR (multiplexer - selects ONE input to output)
sel_in = code.add_in_selector(selector=Ref("selector"), 
                                options=[Ref("one"), Ref("two"), Ref("zero")], 
                                en=clk.out[0],
                                alias="sel_in")


# Test Q_SELECTOR (demultiplexer - activates ONE of N outputs)
sel_q = code.add_q_selector(selector=Ref("selector"), 
                             output_count=3, 
                             en=clk.out[0],
                             alias="sel_q")

set = code.add_set(target=Ref("selector"), value=sel_in.out[0], en=clk.out[0])

m1 = code.add_math(expression='"gains[0]"+0', en=sel_q.out[0])
m2 = code.add_math(expression='"gains[1]"+0', en=sel_q.out[1])
m3 = code.add_math(expression='"gains[2]"+0', en=sel_q.out[2])

code.generate("test_dump.txt")
