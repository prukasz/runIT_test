from Code import Code
from Enums import mem_types_t
from MemAcces import Ref

code = Code()

# ── Variables ──────────────────────────────────────────────
code.var(mem_types_t.MEM_F, "selector",data=0)    
code.var(mem_types_t.MEM_I16, "zero",  data=0)
code.var(mem_types_t.MEM_F, "one",     data=1)
code.var(mem_types_t.MEM_U8, "two",    data=2)
code.var(mem_types_t.MEM_B, "enable", True)

# User-created array: 10-element float array
code.var(mem_types_t.MEM_F, "gains", data=[1,2,3,4,5,6,7,8,9,10], dims=[10])
code.var(mem_types_t.MEM_U8, "gains2", data=[1,2,3,4,5,6,7,8,9,10], dims=[10])
code.var(mem_types_t.MEM_U8, "gains3", data=[1,2,3,4,5,6,7,8,9,10], dims=[10,10])
code.var(mem_types_t.MEM_U8, "gains4", data=[1,2,3,4,5,6,7,8,9,10], dims=[10,10,10])
# ── Blocks ─────────────────────────────────────────────────
ton = code.add_timer(pt = 10000, alias="ton", en = "enable")

clk  = code.add_clock(period_ms=5000, width_ms=1000, en=ton.out[0], alias="clk")

latch = code.add_latch(set=ton.out[0], reset="zero", en="enable", latch_type=0, alias="latch")

# Test IN_SELECTOR (multiplexer - selects ONE input to output)
sel_in = code.add_in_selector(selector=Ref("selector"), 
                                options=[Ref("one"), Ref("two"), Ref("zero")], 
                                en=latch.out[0],
                                alias="sel_in")


# Test Q_SELECTOR (demultiplexer - activates ONE of N outputs)
sel_q = code.add_q_selector(selector=Ref("selector"), 
                             output_count=3, 
                             en=latch.out[0],
                             alias="sel_q")

set = code.add_set(target=Ref("selector"), value=sel_in.out[0], en=latch.out[0])

m1 = code.add_math(expression=' "gains[0]" +0 ', en=sel_q.out[0])
m2 = code.add_math(expression=' "gains[1]" +0 ', en=sel_q.out[1])
m3 = code.add_math(expression=' "gains[2]" +0 ', en=sel_q.out[2])

# ── Subscriptions ──────────────────────────────────────────
sub = code.subscribe("selector", "enable", "gains", sel_q)
forr = code.add_for(expr="i=0; i<99999; i+=1", en=ton.out[0])

code.generate("test_dump.txt", subscriptions=sub)
