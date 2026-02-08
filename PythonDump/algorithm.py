"""
algorithm.py - Topological sorting for emulator blocks.

Given a dict of blocks (keyed by their *original* idx), produces a new
ordering [0 .. N-1] that respects data dependencies and gives SET blocks
priority placement right after the block whose output they consume.

Rules
-----
1. A block with **no block-input dependencies** (only user-variable or
   constant inputs) is a *root* and can be placed first.
2. After placing block *B*, any **SET blocks** that read an output of *B*
   are placed immediately after *B* (they are "sinks" — they write back
   to user variables and don't produce outputs consumed by other blocks).
3. After SET blocks, blocks that depend on *B*'s outputs (and whose
   **all** other dependencies have already been placed) are candidates.
   Among candidates we recurse depth-first so chains stay together.
4. **FOR blocks**: the ``chain_len`` child blocks that follow a FOR in
   execution are the blocks whose ``en`` (input 0) is wired to the FOR's
   ``out[0]``.  They (and their own SET sinks) are placed immediately
   after the FOR, before any unrelated blocks.  Blocks that *transitively*
   depend on those chain children (i.e. they consume an output of a chain
   child) are also placed inside the FOR body.  After sorting,
   ``chain_len`` is **automatically recomputed** to equal the total number
   of blocks placed between the FOR and the next non-chain block.
5. If recursion exhausts but blocks remain, we pick the next root (a
   block whose remaining unplaced dependencies are fewest — ideally 0)
   and continue.

Public API
----------
    sorted_blocks = topological_sort(blocks_dict)
    # blocks_dict: {original_idx: Block, ...}
    # returns: list[Block] in new execution order
    
    reindex(sorted_blocks)
    # Mutates block.idx in-place to [0 .. N-1]
"""

from __future__ import annotations
import re
from typing import Dict, List, Set, Optional
from collections import defaultdict

# Pattern that identifies a block-output alias:
#   numeric form:  "1000_q_0"   (block idx 1000, output 0)
#   named form:    "calc_q_0"   (user alias "calc", output 0)
_BLOCK_OUTPUT_RE = re.compile(r'^(\w+)_q_(\d+)$')


def _build_alias_to_idx_map(blocks: Dict[int, 'Block']) -> Dict[str, int]:
    """
    Build a mapping from every alias prefix that can appear before ``_q_N``
    back to the block's *original* idx.

    For each block with idx=1000 and out.user_alias="calc":
      - "1000" → 1000
      - "calc" → 1000
    """
    alias_map: Dict[str, int] = {}
    for idx, block in blocks.items():
        alias_map[str(idx)] = idx
        if hasattr(block, 'out') and hasattr(block.out, 'user_alias') and block.out.user_alias:
            alias_map[block.out.user_alias] = idx
    return alias_map


# ============================================================================
# 1.  Dependency extraction
# ============================================================================

def _get_block_dependencies(block, alias_map: Optional[Dict[str, int]] = None) -> Set[int]:
    """
    Return the set of *original* block indices that *block* depends on,
    by inspecting every input Ref alias.

    *alias_map* maps alias prefixes ("1000" or "calc") to block indices.
    If not provided, only numeric prefixes are recognised.
    """
    deps: Set[int] = set()
    for ref in block.in_conn:
        if ref is None:
            continue
        m = _BLOCK_OUTPUT_RE.match(ref.alias)
        if m:
            prefix = m.group(1)
            # Try numeric first
            if prefix.isdigit():
                dep_idx = int(prefix)
            elif alias_map and prefix in alias_map:
                dep_idx = alias_map[prefix]
            else:
                continue  # not a block output
            if dep_idx != block.idx:
                deps.add(dep_idx)
    return deps


def _get_consumers(blocks: Dict[int, 'Block'],
                   alias_map: Optional[Dict[str, int]] = None) -> Dict[int, List[int]]:
    """
    Build a mapping: producer_idx -> [consumer_idx, …]
    
    consumer's input refs whose alias matches a producer's output.
    """
    consumers: Dict[int, List[int]] = defaultdict(list)
    for idx, block in blocks.items():
        for dep_idx in _get_block_dependencies(block, alias_map):
            consumers[dep_idx].append(idx)
    return consumers


# ============================================================================
# 2.  Classification helpers
# ============================================================================

def _is_set_block(block) -> bool:
    """Check if block is a SET block (by type enum value)."""
    from Enums import block_types_t
    return block.block_type == block_types_t.BLOCK_SET


def _is_for_block(block) -> bool:
    """Check if block is a FOR block."""
    from Enums import block_types_t
    return block.block_type == block_types_t.BLOCK_FOR


def _get_for_chain_children(for_block, blocks: Dict[int, 'Block']) -> List[int]:
    """
    Return indices of blocks whose EN input (in_conn[0]) is wired to
    for_block.out[0], i.e. the loop-body children.
    """
    # Accept both numeric and named alias forms
    en_aliases = {f"{for_block.idx}_q_0"}
    if hasattr(for_block, 'out') and hasattr(for_block.out, 'user_alias') and for_block.out.user_alias:
        en_aliases.add(f"{for_block.out.user_alias}_q_0")

    children = []
    for idx, block in blocks.items():
        if idx == for_block.idx:
            continue
        if block.in_conn and block.in_conn[0] is not None:
            if block.in_conn[0].alias in en_aliases:
                children.append(idx)
    return children


# ============================================================================
# 3.  Topological sort
# ============================================================================

def topological_sort(blocks: Dict[int, 'Block']) -> List['Block']:
    """
    Produce a list of blocks in valid execution order.
    
    After sorting, every FOR block's ``chain_len`` is **auto-updated** to
    match the number of blocks actually placed inside its loop body
    (direct EN-children *and* their transitive dependents).
    
    :param blocks: dict  {original_idx: Block}
    :return: list[Block] in topological (execution) order
    """
    # --- build alias map and dependency info ---
    alias_map = _build_alias_to_idx_map(blocks)
    deps: Dict[int, Set[int]] = {}          # idx -> set of dependency indices
    consumers = _get_consumers(blocks, alias_map)  # idx -> list of consumer indices
    
    for idx, block in blocks.items():
        deps[idx] = _get_block_dependencies(block, alias_map)

    placed_set: Set[int] = set()
    result: List['Block'] = []

    # Track which FOR block (by original idx) "owns" each placed block.
    # After sorting we count how many blocks each FOR owns -> chain_len.
    # Key = for_block original idx,  Value = list of child original indices
    for_chain_members: Dict[int, List[int]] = {}

    # Stack of active FOR scopes.  When we enter a FOR body, we push
    # the FOR's original idx.  Every block placed while the scope is
    # active belongs to the innermost FOR.
    _for_scope_stack: List[int] = []

    def _is_ready(idx: int) -> bool:
        """All dependencies of idx have been placed."""
        return deps[idx].issubset(placed_set)

    def _place(idx: int):
        """Place block idx into result and mark as placed."""
        if idx in placed_set:
            return
        placed_set.add(idx)
        result.append(blocks[idx])
        # Register this block as a member of the innermost FOR scope
        if _for_scope_stack:
            for_chain_members[_for_scope_stack[-1]].append(idx)

    def _place_block_and_followers(idx: int):
        """
        Place block *idx*, then:
        1. Place any SET blocks that consume its outputs (sinks).
        2. If it is a FOR block, place chain children (and their SETs)
           immediately after, tracking them in for_chain_members.
        3. Recurse into non-SET consumers whose deps are now satisfied.
        """
        if idx in placed_set:
            return
        _place(idx)

        block = blocks[idx]

        # --- Collect consumers of this block's outputs ---
        cons = consumers.get(idx, [])

        # Partition: SET sinks vs. regular dependents
        set_sinks = [c for c in cons if _is_set_block(blocks[c]) and c not in placed_set and _is_ready(c)]
        regular   = [c for c in cons if not _is_set_block(blocks[c]) and c not in placed_set]

        # 1) Place SET sinks first (they don't produce outputs others need)
        for s in set_sinks:
            _place(s)

        # 2) FOR block: open a new scope, place ALL dependents inside it
        if _is_for_block(block):
            for_chain_members[idx] = []          # start tracking
            _for_scope_stack.append(idx)

            # 2a) Direct chain children (EN wired to for.out[0])
            chain_children = _get_for_chain_children(block, blocks)
            for child_idx in chain_children:
                if child_idx not in placed_set and _is_ready(child_idx):
                    _place_block_and_followers(child_idx)

            # 2b) Any other consumers of FOR outputs (e.g. blocks using
            #     the iterator out[1]) that are NOT already placed
            for c in regular:
                if c not in placed_set and _is_ready(c):
                    _place_block_and_followers(c)

            _for_scope_stack.pop()
        else:
            # 3) Recurse into ready regular consumers (depth-first)
            for c in regular:
                if c not in placed_set and _is_ready(c):
                    _place_block_and_followers(c)

    # --- Main loop: pick roots until all placed ---
    while len(placed_set) < len(blocks):
        # Find best candidate: fewest unplaced dependencies
        candidates = [
            idx for idx in blocks
            if idx not in placed_set
        ]
        if not candidates:
            break  # safety

        # Sort by number of *unplaced* dependencies (0 = root)
        candidates.sort(key=lambda idx: len(deps[idx] - placed_set))

        best = candidates[0]
        unplaced_deps = deps[best] - placed_set
        if unplaced_deps:
            # Circular dependency or missing block — place deps first
            for d in sorted(unplaced_deps):
                if d in blocks and d not in placed_set:
                    _place_block_and_followers(d)

        _place_block_and_followers(best)

    # --- Auto-update chain_len for every FOR block ---
    for for_idx, members in for_chain_members.items():
        blocks[for_idx].chain_len = len(members)

    return result


# ============================================================================
# 4.  Re-index helper
# ============================================================================

def reindex(sorted_blocks: List['Block']):
    """
    Mutate ``block.idx`` in-place so that the sorted list has indices
    ``0, 1, 2, …, N-1``.
    
    Also updates:
    - ``block.out`` proxy (BlockOutputProxy.block_idx)
    - output variable aliases in block.q_conn and context
    - input Refs that point to renamed block outputs
    
    NOTE: This is a destructive operation. Use *after* sorting and *before*
    packing packets. 
    """
    # Build old_idx -> new_idx mapping
    idx_map: Dict[int, int] = {}
    for new_idx, block in enumerate(sorted_blocks):
        idx_map[block.idx] = new_idx

    # Build alias prefix -> old_idx  (both numeric and named)
    alias_to_old: Dict[str, int] = {}
    for block in sorted_blocks:
        alias_to_old[str(block.idx)] = block.idx
        if hasattr(block, 'out') and hasattr(block.out, 'user_alias') and block.out.user_alias:
            alias_to_old[block.out.user_alias] = block.idx

    # 1. Rename output aliases in context storage
    #    Both numeric ("1000_q_0") and named ("calc_q_0") entries.
    for block in sorted_blocks:
        old_idx = block.idx
        new_idx = idx_map[old_idx]
        ctx = block.ctx
        user_alias = getattr(block.out, 'user_alias', None)

        for q_num, q_ref in enumerate(block.q_conn):
            old_numeric = f"{old_idx}_q_{q_num}"
            new_numeric = f"{new_idx}_q_{q_num}"
            # Rename numeric alias
            if old_numeric in ctx.alias_map:
                entry = ctx.alias_map.pop(old_numeric)
                ctx.alias_map[new_numeric] = entry

            # Rename / keep named alias (e.g. "calc_q_0")
            if user_alias:
                named = f"{user_alias}_q_{q_num}"
                if named not in ctx.alias_map:
                    # Re-register pointing to the same entry
                    ctx.alias_map[named] = ctx.alias_map[new_numeric]

            # Update the Ref object: if it uses a named alias keep it,
            # otherwise switch to the new numeric alias.
            if user_alias and q_ref.alias.startswith(user_alias + "_q_"):
                pass  # already named, stays valid
            else:
                q_ref.alias = new_numeric

    # 2. Update input Refs that point to block outputs
    for block in sorted_blocks:
        for ref in block.in_conn:
            if ref is None:
                continue
            m = _BLOCK_OUTPUT_RE.match(ref.alias)
            if not m:
                continue
            prefix, out_num = m.group(1), m.group(2)
            if prefix in alias_to_old:
                old_dep = alias_to_old[prefix]
                new_dep = idx_map[old_dep]
                # If prefix is a named alias, keep the named form
                if prefix.isdigit():
                    ref.alias = f"{new_dep}_q_{out_num}"
                # else: named alias stays — the context entry already exists

    # 3. Update block.idx and block.out proxy
    for new_idx, block in enumerate(sorted_blocks):
        block.idx = new_idx
        block.out.block_idx = new_idx


# ============================================================================
# 5.  Convenience: sort + reindex in one call
# ============================================================================

def sort_and_reindex(blocks: Dict[int, 'Block']) -> List['Block']:
    """
    Topologically sort blocks and reassign indices 0..N-1.
    
    :param blocks: dict {original_idx: Block}
    :return: list[Block] with updated .idx values
    """
    sorted_blocks = topological_sort(blocks)
    reindex(sorted_blocks)
    return sorted_blocks


# ============================================================================
# DEMO / STANDALONE TEST
# ============================================================================

if __name__ == "__main__":
    from Enums import mem_types_t
    from Code import Code
    from MemAcces import Ref

    # ==================================================================
    # TEST 1: Simple dependency chain (no FOR)
    # ==================================================================
    print("=" * 60)
    print("TEST 1: Simple dependency chain")
    print("=" * 60)

    code = Code()

    code.add_variable(mem_types_t.MEM_F, "sensor_a", data=1.0)
    code.add_variable(mem_types_t.MEM_F, "sensor_b", data=2.0)
    code.add_variable(mem_types_t.MEM_F, "output", data=0.0)
    code.add_variable(mem_types_t.MEM_B, "enable", data=True)
    code.add_variable(mem_types_t.MEM_F, "threshold", data=50.0)

    set_blk = code.add_set(idx=10, target="output", value=Ref("5_q_1"))
    math_blk = code.add_math(idx=5,
                             expression='"sensor_a" * "sensor_b"',
                             en=Ref("20_q_0"))
    clk_blk = code.add_clock(idx=20, period_ms=1000, width_ms=500, en="enable")
    logic_blk = code.add_logic(idx=15,
                               expression='"sensor_a" > "threshold"',
                               en="enable")

    print(f"Before: {[b.idx for b in code.get_blocks_sorted()]}")
    sorted_blocks = topological_sort(code.blocks)
    print(f"After sort: {[b.idx for b in sorted_blocks]}")
    reindex(sorted_blocks)
    print(f"After reindex:")
    for b in sorted_blocks:
        deps = _get_block_dependencies(b)
        print(f"  idx={b.idx} ({b.block_type.name:15s}) deps={deps}")
    print("Expected: CLOCK(0), LOGIC(1), MATH(2), SET(3)")

    # ==================================================================
    # TEST 2: FOR chain with transitive dependent
    # CLOCK -> FOR(chain_len=?) -> MATH(EN=for.out[0]) -> LOGIC(uses math.out[1]) -> SET(sink)
    # All of MATH, LOGIC, SET should be inside FOR body => chain_len = 3
    # ==================================================================
    print("\n" + "=" * 60)
    print("TEST 2: FOR chain with TRANSITIVE dependents")
    print("=" * 60)

    code2 = Code()
    code2.add_variable(mem_types_t.MEM_B, "enable", data=True)
    code2.add_variable(mem_types_t.MEM_F, "sensor", data=5.0)
    code2.add_variable(mem_types_t.MEM_F, "result", data=0.0)

    # Block 99: CLOCK (root)
    code2.add_clock(idx=99, period_ms=1000, width_ms=500, en="enable")

    # Block 20: FOR (EN = clock.out[0]), user sets chain_len=1 (WRONG — algo should fix)
    code2.add_for(idx=20, chain_len=1,
                  expr="i=0; i<10; i+=1",
                  en=Ref("99_q_0"))

    # Block 30: MATH (EN = for.out[0]) — direct chain child
    code2.add_math(idx=30,
                   expression='"sensor" + 1.0',
                   en=Ref("20_q_0"))

    # Block 40: LOGIC (uses math.out[1], NOT EN-wired to FOR) — transitive
    code2.add_logic(idx=40,
                    expression='in_1 > 10.0',
                    connections=[Ref("30_q_1")],
                    en=Ref("20_q_0"))

    # Block 50: SET (sink of LOGIC output) — transitive of transitive
    code2.add_set(idx=50, target="result", value=Ref("40_q_1"))

    for_block = code2.blocks[20]
    print(f"chain_len BEFORE sort: {for_block.chain_len}")
    print(f"Block order before: {[b.idx for b in code2.get_blocks_sorted()]}")

    sorted2 = topological_sort(code2.blocks)
    print(f"After sort: {[b.idx for b in sorted2]}")
    print(f"chain_len AFTER sort: {for_block.chain_len}")

    reindex(sorted2)
    print(f"After reindex:")
    for b in sorted2:
        deps = _get_block_dependencies(b)
        extra = ""
        if hasattr(b, 'chain_len'):
            extra = f" chain_len={b.chain_len}"
        print(f"  idx={b.idx} ({b.block_type.name:15s}) deps={deps}{extra}")
        for i, ref in enumerate(b.in_conn):
            if ref:
                print(f"      in[{i}] -> {ref.alias}")

    print(f"\nExpected order: CLOCK(0), FOR(1), MATH(2), LOGIC(3), SET(4)")
    print(f"Expected chain_len: 3  (MATH + LOGIC + SET inside FOR body)")
    print(f"Actual   chain_len: {for_block.chain_len}")
    assert for_block.chain_len == 3, f"FAIL: chain_len={for_block.chain_len}, expected 3"
    print("PASS ✓")

    print("\n" + "=" * 60)
