# Object Ownership, Schema & Mutation Contracts

Technical rules governing object lifetime, graph ownership, schema matching, and mutation policies. API surface detailed in [VM_OBJ_ACCESS.MD](VM_OBJ_ACCESS.MD).

---

## 1. Program Lifetime & Barrier

- **Lifecycle Boundary**: `vm_loader_reset()` and `vm_loader_open()` run exclusively on the control task (never inside blocks or sample hooks).
- **Execution Barrier**: Halts pass admission, releases frozen passes, and waits for the active scan to return before reclaiming storage.
- **Atomic Open**: Allocates and validates the new pool before destroying the existing program; failure preserves prior program, subscriptions, and execution mode.
- **Teardown Scope**: Reset clears execution state, subscriptions, dynamic heap objects, registries, and the bump arena.
- **Handle Expiry**: All `vm_obj_h` handles, resolved payloads, and accessors expire on program replacement.

---

## 2. Ownership Graph & Dynamic Objects

- **Reference Counting**: Each pointer slot targeting a dynamic object holds 1 reference; dynamic objects may be shared across multiple parents.
- **DAG Enforcement**: Dynamic links must form an acyclic graph with depth `<= VM_DYN_MAX_DEPTH` (16).
- **Link Boundary**: `vm_obj_link()`, `vm_obj_link_direct()`, and CLONE validate cycle freedom and depth bounds across all ancestors before updating pointer slots.
- **Arena vs Dynamic**: Arena objects live until program reload; arena slots holding dynamic children serve as independent ownership roots.
- **Reclamation**: New dynamic objects start at refcount 0; linking increments refcount, replacement decrements. Objects reaching refcount 0 cascade release to dynamic children.

---

## 3. Storage Shape & Schema

- **Shape Matching (`vm_obj_shape_matches`)**: Compares element type, item count, and child slot positions for memory compatibility.
- **Schema Matching (`vm_obj_schema_matches`)**: Extends shape comparison to require identical tag names, lengths, and bytes at every node.
- **Clone Reuse**: Reuses destination memory in-place when full schema matches; reallocates new dynamic trees if tags or shapes differ.
- **Traversal Bound**: Copy and Clone bound pointer traversal to at most 8 levels.

---

## 4. Mutation & Protection Policies

- **Mutability**: Internal scalar, copy, and link APIs require `mutable` on target objects.
- **User Protection**: The `_usr` APIs reject targets with `usr_protected` set (enforced by `SET` and `CLONE` blocks).
- **Output Protection**: Block construction automatically sets `usr_protected` on all output pins and ENO.
- **Two-Phase Copy**: Preflights destination types, counts, and write permissions before writing; validation failure leaves destination untouched.
- **Atomic Clone**: Follows **Build → Fill → Validate Link → Publish → Release**; errors discard the replacement without altering the prior destination.

---

## 5. Freshness Scope

- **Scalar Write**: Marks the object owning target bytes updated (`f.upd = 1`).
- **Pointer Link**: Marks the container object holding the slot updated.
- **Deep Copy**: Marks destination root and all written descendant objects.
- **Clone**: Marks replaced destination and its containing pointer cell.
- **Aggregate Publication**: Updating a child does not mark parents; complete aggregates are published explicitly via `vm_obj_publish()`.
- **Quiet Operations**: `vm_obj_clear_quiet()` and inactive blocks (`vm_block_set_ENO(b, false)`) modify state without asserting `f.upd`.
