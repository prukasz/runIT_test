# VM Execution Model & Target Look

This document specifies the execution model and runtime semantics for the runIT VM:
- **Core Engine**: Implemented in `core/exec/` and `core/block/` (see [[VM.MD]]).
- **Execution Units**: Topological block-by-block scan passes with span jumps for nested flow control.
- **Palette**: Dispatch table in `blocks/` providing concrete block behaviours (`EXPR`, `BRANCH`, `FOR`, `SET`, `CLONE`, `EDGE`, `TIMER`).

---

## 1. The user's model

### Three kinds of connection

A user never draws an enable wire as a separate thing, and never sees the words
EN or ENO. What they draw carries the meaning:

| gesture | ordering | gating |
| :--- | :--- | :--- |
| wire into a **data port** (`MAP.OUT → SERVO.ANGL`) | yes | no |
| wire into the **EN port** (`A>B.RES → SERVO.EN`) | yes | yes |
| **label** reference (`G_B → A>B`) | no | no |

A data wire says "run after this, and read its value". An EN wire additionally
says "…and only if it passed the flow on". A label is a pure global read — no
ordering, no gating — the escape hatch for "I want the sensor reading but I am
not part of that chain".

Note **ungated and unordered are different**. `MAP` has a data wire in and no EN,
so it always runs, but still runs after the `ADC` feeding it. Only a block whose
every input is a label has no predecessors at all.

### Two activation groups

Some blocks only make sense when something *new* arrives — an event router has
nothing to do if no event came. Forcing those to carry an EN wire alongside the
data wire would mean two wires between the same pair of blocks, every time, for
no added information. So the block decides how it activates:

| group | activates when | EN port shown | examples |
| :--- | :--- | :--- | :--- |
| **update-driven** | an input it cares about is *fresh* | optional | `CASE IO`, event/message routers |
| **enable-driven** | its EN sources say so (`en_cnt == 0` ⇒ always) | yes | `SERVO ANGLE`, `SET PIN`, `TOGGLE PIN` |

The two **compose**: a block may be both, and then activates only when a trigger
input is fresh **and** its EN list allows — "route this event, but only while
armed".

These are two shapes of block body, not two kinds of block. Nothing outside the
block records which group it is in, so composing them costs nothing and needs no
rule — the body tests whatever it likes, in whatever order. See the freshness
section below.

`ADC`, `MAP`, `A > B` and expression blocks are enable-driven with no sources, so
they simply always run. No third category is needed.

**A block that does not activate keeps its outputs and drops its ENO** — it
does not merely skip, and it does not wipe its data either.

An earlier draft here said it drives its outputs *to their defaults*. That is
wrong for the common case and right only for a minority. Nothing changed, so
last pass's answer is still the right one: an expression whose inputs are all
old computes the same result it computed last pass, and zeroing it would
publish a 0 where a correct number was standing. **Data persists.** What is
withdrawn is the *flow*, and ENO is what carries that.

A later draft carved out an exception for outputs carrying flow rather than
data — a router's `YES`/`NO` — on the grounds that a router which fired once
would leave `YES` true forever. **That exception is gone too.** It only bites if
something reads `YES` as a *level*, and an event router's consumers are
update-driven: they gate on freshness, which the supervisor withdraws for
everybody in its end-of-pass `upd` sweep. The exception was solving a problem
the sweep had already solved.

The write is **quiet**: it changes ENO *without* marking it fresh. A drop that
set `upd` would look like new data to any update-driven block downstream, and
one event would ripple outward for a second pass. Note the asymmetry, and that
it is deliberate: **ENO true is loud** — it is news — **ENO false is quiet**,
because it is the absence of news.

In code that is `vm_block_set_ENO(b, false)`, which owns the asymmetry so no
block body can get it wrong, and which is the whole of the else-branch.

### A forest of graphs

Multiple roots, arbitrary branching, and **merges** — a block may have several
incoming wires. Each root carries a **trigger** on its face, the only scheduling
concept the user ever sees:

```
┌─ On GPIO 4 rising ────────┐   ┌─ Every 100 ms ───────────┐
│  Read sensor      → [v]   │   │  Read pressure   → [p]   │
│  Wait 50 s                │   │  If p > 2.0              │
│  If v > 100               │   │    └ Set valve  closed   │
│    ├ Set servo   90°      │   │       when inactive: Hold │
│    └ Set lamp    on       │   └──────────────────────────┘
└───────────────────────────┘
```

Branches run **top to bottom, depth first**. Position on the canvas is a
*tie-break*, not the rule: data dependencies win, so a block always runs after
whatever feeds it regardless of where it sits. Top-to-bottom decides only between
blocks the ordering leaves free.

The editor should therefore lay out (or warn) so visual order always matches
computed order — it runs the sort anyway. Otherwise a user can drag a block above
its own producer and the canvas will be lying about what happens first, which is
the one thing this layout is supposed to guarantee.

### One primitive behind four features

"Wait 50 s", "Every 100 ms", "If A and B", "Stop here" are all the same thing: a
block that decides whether the flow continues below it. No scheduler involvement,
no new mechanism, and they compose in any order because each only knows about the
blocks feeding it.

### Everything else the user sees

* **Activate when** is a visible field on the block face, listing the incoming
  links from other blocks. Node-style: **any** of them arriving activates the
  block. The user reads what enables a block off its own face rather than
  tracing wires backwards.
* **Constants** live in the block face (`Set servo [90]°`), not as wired blocks.
* **Types** are visible, and permissive by default — float→int rounds, with a
  warning badge on the wire. A strict mode turns warnings into refusals. Strings
  are always guarded from numerics.
* **Actuators** carry a visible `when inactive:` choice, and the program does not
  validate until it is chosen — no silent hold, no silent home.
* **Errors** appear on the canvas on the failing block, alongside subscribed live
  values, with detail in a log.
* **Live values** appear for explicitly subscribed ids, not everything.

---

## 2. Execution architecture

### Where order comes from

Merges make the flow graph a **DAG, not a tree**. The client resolves it before
upload: walk the wire graph, find blocks with no incoming flow edge (roots),
**topologically sort** each connected component, and emit it as an ordered list of
block ids. Canvas position breaks ties between blocks the sort leaves unordered,
so the order the user reads top-to-bottom is the order that runs.

**The device never sorts.** The whole forest — standalone blocks and long chains
alike — merges into **one** top-to-bottom order for the program. There is no
per-section ordering, because there is only one order.

That makes the **block registry index the execution order**: the client sorts,
uploads its `0x45` frames in that order, and the supervisor walks ids `0..n-1`.
`FOR` carries its `[start, end)` range in its own `custom_data`, since a span is
the one thing a flat order cannot express.

### Sections are atomic units, not schedules

Sections still exist, for a different job than ordering: a section is the unit
that **runs all at once**, and therefore the only place the system may be
interrupted. Freeze, resume and the double-buffered swap take effect at a section
boundary — never mid-section, where half the blocks would have acted on new
inputs and half on old.

So a section is a `[start, end)` range over the one global order, exactly like a
`FOR` span. It says nothing about *whether* or *when* its blocks run — every
section runs every pass — only that once started it finishes before anything may
intervene.

It also settles label ordering. A label read *below* its writer sees this pass's
value; *above* it, last pass's. Not a race — a position, readable off the canvas.

### Enable is a list, with a mode

A block carries **0..N enable sources** — surfaced to the user as the block's
`activate when` field:

| sources | meaning |
| :--- | :--- |
| 0 | root — always enabled |
| 1 | ordinary chain link; mode is irrelevant |
| N | `any` — branches rejoining, either path reaching the block runs it |
| N | `all` — independent conditions, every one must hold |

Both modes are needed and neither is a safe default for the other. Two branches
of one split rejoining is an OR. "On rising edge **and** while `A > B`" — the
sample-on-trigger pattern — is an AND, and running it as an OR would move the
actuator on every edge regardless of the condition. The canvas draws both as
wires into `EN`, so the mode is a visible selector on the block face rather than
something inferred from shape.

A source that fails to resolve counts as *not* enabled, so the fail-closed rule
survives: under `any` one broken branch cannot mask a working one, and under
`all` a broken condition correctly stops the block.

Absence and unresolvability are opposites on purpose — zero sources is a root and
always runs, while a source that exists but cannot be read blocks.

### Freshness is consulted on declared trigger inputs only

An update-driven block must not fire because some unrelated global it reads
happened to be written. This draft answered that with a per-type descriptor
carrying a `trigger_mask`, so the *wiring* could not decide which inputs
activate.

**What was built is simpler: the block asks.** A block type is a function and
nothing else —

```c
typedef void (*vm_block_fn)(vm_block_h);
static const vm_block_fn s_blocks[] = {           // the entire palette
    [BLK_CASE_IO] = blk_case_io,
};
```

— and the supervisor calls **every** block, deciding nothing. A block that has
nothing to do without new data opens with `vm_block_triggered()`, which answers
"did anything arrive on any of my inputs", and stands itself down by dropping
its ENO and nothing else:

```c
static void blk_case_io(vm_block_h b) {
  if (!vm_block_triggered(b) || !vm_block_is_enabled(b)) {
    vm_block_set_ENO(b, false);
    return;
  }
  ...
}
```

The descriptor's whole purpose was to tell the supervisor something about a
block it could not otherwise know — but the block knows it, and holds
everything needed to answer: its pins, its enable list, its private state. So
the mask is not stored anywhere, per type or per instance, and
`vm_block_input_fresh(pin)` covers the case where a block needs to know *which*
input woke it. A block that runs on level rather than arrival never asks, and
pays nothing for the feature existing.

The two groups therefore stop being a property of the type at all. They are
just two shapes of block body, and they compose for free: a block that wants
"route this event, but only while armed" tests both, in the order it likes.

One editor rule survives unchanged: **a trigger pin must be fed by a wire, not
a label.** A label gives freshness without ordering, so the block could observe
the update a pass late — a race, and a confusing one.

### `upd` is cleared by the supervisor, not the consumer

Freshness rides on `head.f.upd`, already set by every write. The clearing rule is
the part with a failure mode: if a consumer cleared the flag when it ran, fan-out
would break — two blocks reading the same fresh object, and whichever ran first
would steal the update.

So the supervisor clears `upd` at the end of a **pass** — not at the point of
consumption, and not at each section boundary. Topological order already
guarantees a writer runs before its readers within the same pass, so every
consumer sees the flag before it is cleared. Clearing per section would break
that across sections: a value written in section 0 would no longer read as fresh
in section 3, even though section 3 runs after it in the same pass. Cost is one
sweep of the object registry per pass.

**The sweep must respect `upd_resetable`.** That header bit already exists and is
carried over the wire (`VM_LOAD_F_UPD_RESETABLE`). A non-resettable object is
never cleared by anyone — the flag is reserved for constants and user variables,
where freshness is not a meaningful signal.

A non-resettable object *may* still be used as a trigger input — but since its
`upd` never clears, the block reading it is permanently fresh. That is not
rejected; the block is simply **marked always-active** in the editor, so the user
sees the consequence on the block face rather than being blocked by a rule or
surprised by a block that never stops firing.

Order within the pass matters: **run sections → sample subscriptions → clear
`upd`.** Clearing before telemetry would silently break "send on update", which
is the flag's other customer.

### Event payloads are valid for the pass that published them

An event object holds the *latest* payload, not the one that activated a given
block. Within a pass that is the same thing — the buffer is swapped once, at the
top, so a pass is a single instant for arrivals exactly as it is for the clock —
so linking an event output onward freely is fine and needs no copy. Across
passes it is not: anything behind a time gate resumes later, by which point a
newer payload may have landed. There, take an explicit **Copy** in the
triggering pass.

That is a rule the editor can check exactly — if a path from an event output to a
consumer crosses a time-gate block, require a Copy — which is far narrower than
banning the link outright.

### Triggers are blocks, not section headers

An earlier draft gave each section a trigger descriptor and routed events to
sections through a lookup table. The update-driven group removes the need for all
of it.

`ON EVENT` is an ordinary block that walks this cycle's routed callbacks for one
it recognises and, if it finds one, publishes it to its output **loudly** — setting
`upd`, because an arrival genuinely is news. `CASE IO` below it then fires
because its input is fresh. Nothing had to be scheduled: no trigger descriptor,
no event→section routing, no per-section run decision, and nothing that has to
know which block an event belongs to.

Routing at a *buffer* rather than at a block is what closes the last of it. An
event source names what happened; blocks decide what they recognise. Neither has
to know about the other, and `sys_callbacks`' existing route mask already does
the delivering — the VM adds one slot to a table that was already there.

Time-of-day, GPIO edge, program start and manual run are all the same shape:
blocks that publish, or that pass the flow on when their condition holds.

### The supervisor loop

Free-running — power management is explicitly not a goal. The pass alternates
between running a section and a **freeze point**:

```
freeze → run section 0 (atomically) → freeze → run section 1 → … → end of pass
```

A section runs to completion with nothing intervening. Everything external
happens at a freeze point: pending events drained into their event blocks,
remote overrides applied, live data patches committed, the buffer swap taken.
Freeze and resume take effect there too, which is the point of section
granularity — freeze latency is bounded by the longest *section*, not by a
whole pass over 300 blocks.

At end of pass: sample subscriptions, then clear `upd`.

There is no scheduling policy to state and no starvation to counter — every
section runs every pass, and the work saved is inside the blocks rather than in
choosing between them.

**Runs on core 1.** The VM gets the second core so a tight loop cannot starve
BLE, which lives on core 0. That is not quite enough on its own: ESP-IDF watches
the idle task of *both* cores (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y`,
5 s), and `taskYIELD()` will not feed it because idle runs at priority 0. So the
loop yields one tick per pass — with `CONFIG_FREERTOS_HZ=100` that is exactly the
10 ms floor already promised, and it leaves core 1 usable by device drivers.

Because there is no sleep, timing needs no deadline heap and no timer to re-arm:
time-based blocks simply compare timestamps on each pass, and the 10 ms floor is
set by how fast a full pass completes, not by a tick.

Everything runs on the supervisor's own task. A callback or ISR only enqueues; it
never touches a block, an object or the arena. That keeps the entire
store/accessor layer lock-free, and it is why "override only after a cycle, never
mid" falls out automatically: overrides, patches and events all apply at the same
drain point, so a section always traverses a consistent snapshot.

### Within a section

Every block in a running section is called, including inactive ones — that is
what makes `when inactive:` work at all, and it is literal: the supervisor runs
no test of its own. Inactive means the block runs its disabled branch, which
for every block is `vm_block_set_ENO(b, false)` — "do not pass the flow on" —
pruning everything downstream by data rather than by control flow. Its outputs
stand, and the end-of-pass `upd` sweep withdraws their freshness for it.

An actuator with a visible `when inactive: Hold` therefore needs no mechanism:
it is called like every other block and simply does not drop its ENO. There
is no flag for it, and nothing to declare.

**FOR is a supervisor of its own sub-range.** It owns a `[start, end)` span of the
section's ordered list and walks that span N times within one pass. N must be
bounded at load so a section's worst-case duration stays computable.

The span lives in the FOR's **own private state**, like any other parameter —
nothing outside the block describes it — and the block **claims** it at run
time with `vm_block_claim_span()`. The walk reads that claim afterwards. Two
consequences:

* **It must jump over a FOR's range**, not fall into it. Otherwise every block in
  the span runs once from the outer walk *plus* N times from the FOR. Which
  means a FOR must claim **first and unconditionally**, before it checks
  anything: a disabled FOR that returns early without claiming leaves the walk
  about to run the span inline, and running a span zero times is not the same
  as letting somebody else run it once.
* **Nesting needs a depth bound**, for the same reason accessor chains have one:
  a section comes off the wire and nothing stops it declaring FORs inside FORs
  until the stack is gone.

This is why a section packet has to carry extents, not just a flat id list —
pruning works without structure (a disabled block's dependents self-skip), but
repetition genuinely needs the walker to know where a span ends.

### Transient activation needs a latch to become a level

An event enables for exactly one pass, so anything time-based below it can never
accumulate: a `Wait 50 s` downstream of an event sees its enable drop the very
next pass and resets to zero. Event → wait → act is therefore not expressible
directly.

The palette answers it with **latch** blocks (and pulse blocks with a negated
output), which the user places explicitly to convert a one-pass pulse into a held
level. The same block solves actuators below an event, where `when inactive:` is
otherwise the only thing keeping the output from snapping back on the next pass.

Nothing prevents the mistake — a `Wait` below an event simply never completes.
The diagnosis route is **subscribing to the block's state**: the user watches the
gate and sees it never advancing, rather than being warned up front. That is a
debugging surface rather than a guard, and it is the sharpest edge in the model.

### Timing

* Honest floor is **10 ms**; nothing finer is promised to the user.
* "Wait 50 s" is a **gate**, not a suspension — the section keeps running, the
  block passes the flow only after 50 s of continuous enable. Enable dropping
  **resets** the timer to zero.
* "Every 100 ms" is average-rate with **time stretching**: the deadline advances
  by a fixed period so there is no drift, a late run goes as soon as possible,
  and a run that would exceed the declared frequency is **dropped and flagged**
  rather than bursting to catch up.
* Durations surviving reboot is per-value, decided by the NVS retention flag.

### Events: one scan cycle, and blocks ask

> **Activation is transient. Data is persistent.**

That one rule is what makes "runs once when the JSON arrives, but the JSON stays
readable" non-contradictory, and events are the transient half — transient in
the strongest sense available: **an event lives exactly one scan cycle.**

A queue and a snapshot. `sys_callbacks` routes into the queue whenever it likes;
the supervisor drains the whole of it into an array at the top of a pass, and
blocks read that. So a pass sees one unchanging snapshot of everything that
arrived while the previous pass ran, and **whatever it does not act on is gone**
at the next drain.

That also means the VM holds no lock of its own. The queue arbitrates between
`sys_cb_task` and the supervisor — two tasks on two cores — and once the drain
has happened this cycle's events live where only the supervisor can see them,
so a block's reads are lock-free by construction rather than by argument.

Events can therefore go unhandled, on purpose. A block that was disabled when
its event arrived simply misses it, which is the same answer a physical edge
gives — and the alternative, an event waiting indefinitely for a block that may
never be enabled again, is a leak with a nicer name.

**Nothing is delivered, and nothing is addressed to a block.** An event block is
not woken and not routed to. It runs like every other block, walks this cycle's
routed callbacks, and either recognises one and acts or stands down with no ENO
and nothing marked fresh. Nothing is consumed either, so two blocks watching the
same GPIO both see the edge — there is no ownership to arbitrate.

What is buffered is `cb_event_t`, the system's own callback event exactly as
[[SYS_CALLBACKS.MD]] routed it. There is no VM event type: a VM-side struct
would be a translation step needing extension every time a new callback kind
appears, and would throw away what the head already carries. The VM is route
slot `SYS_CB_ROUTE_VM`, and unlike every other slot it does not *handle* the
event — it parks it for a cycle. So an event source needs to know nothing about
the program: a driver reports "GPIO 4 rose" and any number of blocks may act on
it, or none.

An earlier draft here queued entries across passes, each routed at the block
that owned it, one popped per pass so that N events produced N runs. The drain
replaces all of it — no per-entry lifetime, no sequence numbers, no visibility watermark, no
ordering maintained across passes. What is given up is the N-events-N-runs
guarantee: a burst arriving inside one cycle is offered to the block once, and
the rest of the burst is dropped. That is the trade, taken deliberately, and the
cost of the mechanism going wrong is one lost event rather than a corrupted
stream.

Depth is a fixed **number of entries per cycle**. Overflowing *that* — more
arriving in one cycle than the buffer holds — **drops and raises a system
error**, never silent, never unbounded. Events dropped by the drain for want of
a taker are normal, and silent.

### Memory: the client sizes everything

Every pool is allocated **before** blocks load, at exactly the size the client
declares — per element kind (objects, accessors, blocks, sections), in the open
packet. The device does not estimate and does not grow anything. A block header
is **16 bytes** plus 4 per input, output and enable source.

The real limit is therefore contiguous internal DRAM at load time, not a
compile-time budget. A program that does not fit fails cleanly with
`ERR_VM_LOAD_TOO_BIG`, carrying both what was requested and the largest block
actually available, so a fragmented heap is distinguishable from a genuinely
oversized program. `VM_STORE_MAX_POOL` (128 kB) is only a sanity ceiling against
a corrupt declared size.

### Guarded objects, per-block error policy, and the block watchdog

Three enforcement points, all with the state already in place and the check
missing.

**`usr_protected` guards objects user logic must not write.** Block construction
sets it on output objects and ENO. Internal APIs require `mutable`; SET and CLONE
use the `_usr` mutation boundary, which also rejects protected destinations.
Protection applies to each written object, with whole-copy validation before
any writes. The reserved header bit preserves existing defined wire bits.

**On-error behaviour is a per-block flag** — `cfg.on_error`, `VM_BLK_ERR_STOP`
(publish a false ENO, so everything downstream self-skips) or
`VM_BLK_ERR_CONTINUE`. **Implemented** in the struct and the wire format; what
enforces it is the supervisor, which does not exist yet.

**One global watchdog, not per-block timing.** Timing every block would cost a
clock read per block per pass for something that almost never fires. Instead the
supervisor publishes *which block it is currently executing* to a volatile, and a
timer ISR samples it: if the same block is still current after ~20 ms, raise the
alarm. Cost is one store per block and no clock reads at all, and unlike the task
watchdog it names the block that hung instead of just resetting.

Two things to get right when building it. The ISR must distinguish "same block
still running" from "same block again next pass" — a monotonically increasing
sequence number alongside the block id, rather than the id alone. And the ISR
must not *report* the error itself: error construction allocates, which is not
ISR-safe. It sets a flag and something at task level builds the trace.

### Invoking an action

A block type that calls `sys_actions_invoke(id)` with the id in its own
`custom_data`. That is the whole VM↔actions binding: safe states, e-stop,
freeze and power-on behaviour are [[SYS_ACTIONS.MD]]'s, and the VM reaches them
through one ordinary block rather than any dedicated mechanism.

### Conflicting writers

Two sections writing the same actuator is allowed. For hardware blocks the
**last writer in execution order wins** — deterministic, since section order and
in-section order are both fixed at load. The editor should still note the overlap,
because it is far more often a mistake than an intent.

### Safety belongs to actions, not the VM

Device-level safe state, e-stop, freeze-on-reload and power-on behaviour are
[[SYS_ACTIONS.MD]] concerns — ids 1–5 already bind the device sweeps
(freeze/resume/suspend/reset/hard-reset). A block invokes an action; the VM does
not own actuator safety.

This gives dry-run for free: **freeze mode** is the driver sweep, so logic runs
and outputs do not move.

### Lifecycle

The implemented ownership and mutation rules are in
[VM_OBJ_CONTRACTS.md](core/obj/VM_OBJ_CONTRACTS.md). Reset and open use a lifecycle
barrier: stop admitting passes, release a frozen pass, and wait for the active
pass to finish before reclaiming handles. Successful replacement clears dynamic
objects, subscriptions, registries, and runtime state and leaves execution
stopped. Failure to reserve a new pool preserves the prior program and run mode.
Lifecycle calls belong to the control task, never to a block or sample hook.

* **Live patch** of data-only objects while running — applied at the drain point, same as an override. Structural change is a full reload.
* **Retention** is per-value, either automatic or hand-picked via NVS.
* **Single Active Program**: The device holds one active program in memory; stored packet sets can be replayed to switch programs.
* **Step mode**: Run exactly one scan, then stop.
* Trends/history are explicitly out of scope for the MCU.

---

## 3. Architecture Summary

Key execution principles implemented in the VM:
- **Execution Order**: Block-by-block in topological scan order; loops and conditional branches run nested blocks via spans (`vm_block_claim_span`).
- **Composite Gating**: Blocks compose update-driven (`vm_block_input_fresh`) and enable-driven (`vm_block_is_enabled`) activation.
- **Quiet Non-Activation**: Inactive blocks call `vm_block_set_ENO(b, false)`, quietly withdrawing flow without wiping data or triggering downstream update sweeps.
- **User Protection**: Block construction automatically sets `usr_protected` on output objects and ENO, preventing mutations from user scripts.
- **Supervisor Task**: Core-1 FreeRTOS task running at 1 tick per pass (`vm_exec_pass`), orchestrating span jumps, error policies (`cfg.on_error`), and telemetry hooks.
- **Watchdog & Timing**: Volatile sequence/block watchdog sampled via 20 ms `esp_timer`; scan timestamp latched per pass instant via `vm_now_ms()`.
- **Three Registries**: Flat 10-byte open packet initializes Object, Accessor, and Block registries (`VM_REG_OBJ`, `VM_REG_ACC`, `VM_REG_BLK`).

---

## 4. To measure, not to decide

Nothing in the model above is open. One number is worth measuring before the
palette is sized: a ~300-block program of the shape [[VM.MD]]'s budget assumes
wants roughly **77 kB in one contiguous internal-DRAM block**. That is under the
128 kB ceiling and the failure mode is already clean — the load is rejected with
requested-vs-largest-available — so this is a deployment reality to check on real
hardware, not an architecture problem to solve.
