# Object ownership, schema and mutation contracts

These contracts apply to the object API, loader, and blocks. The public calling
surface is listed in [VM_OBJ_ACCESS.MD](VM_OBJ_ACCESS.MD).

## Program lifetime

`vm_loader_reset()` and `vm_loader_open()` are the program lifecycle boundary.
They run on the control task, never inside a block or a telemetry sample hook.
They serialize lifecycle operations, stop admission of new passes, release a
frozen pass, and wait for the current pass to return before replacing storage.
A hung block prevents reclamation; its watchdog can still report the hang.

Reset clears execution state, subscriptions, dynamic objects, registries, and
the arena, leaving execution stopped. The subscription sender and sampling hook
are transport/service configuration and remain installed.

Open validates registry capacity and allocates the new pool before destroying
the old one. Failure preserves the old program, subscriptions, dynamic objects,
loader state, and prior run mode. Success clears program-specific state and
leaves a stopped program ready for upload.

`vm_store_reset()` owns reclamation of **both allocation domains**. Its direct
API is for a quiescent caller; firmware should use the loader lifecycle API.
`vm_obj_dyn_reset()` is an allocator teardown primitive, not a way to clear
live data: any pointer slots naming those objects become invalid.

All object handles, resolved payloads, and arena accessors expire on replacement.
Cached accessors are freed with their program; a raw cached handle does not become
safe to use just because the registry has been cleared. No handles may be retained
by callbacks across program lifetimes. Object access within a running program is
confined to the supervisor task. Upload calls are serialized by the control task,
and initialization packets are refused unless execution is stopped. Runtime
remote-write transport is not implemented by the initialization API.

## Ownership graph

Each pointer slot naming a dynamic object holds one reference. Shared children
are allowed. Dynamic-to-dynamic owning edges must form a DAG with at most
`VM_DYN_MAX_DEPTH` (16) dynamic nodes along any path.

`vm_obj_link()`, `vm_obj_link_direct()`, the user link API, and Clone publication
all use the same slot replacement boundary. Before changing a dynamic parent's
slot, the boundary validates the proposed graph across all registered dynamic
objects. This includes existing ancestors, so extending a chain from its tail
cannot evade the depth limit. The check memoizes completed subtree heights and
detects back edges. Rejected links change neither pointers, counts, nor freshness.

Arena objects live until program teardown. An arena slot holding a dynamic child
is an independent ownership root; a dynamic slot pointing to an arena object does
not own the arena's lifetime and release stops there. Arena graphs may contain
cycles. Copy and Clone still bound traversal through **all** pointer objects.

Runtime callers must not write pointer payload bytes directly. Clone's private
builder initializes only newly allocated, unpublished children, which cannot
contain a back edge and are already bounded by its traversal limit.

New dynamic objects begin at reference count zero. Link them during the same
operation or release them on failure. Replacing a slot retains its new child
before releasing its old child. Never call `vm_obj_dyn_id()` on a freed handle;
retain the registry slot number before releasing if testing reclamation.

## Storage shape and schema

`vm_obj_shape_matches()` compares type, element count, and wired/unwired child
positions. It describes storage compatibility, independently of names and flags.

`vm_obj_schema_matches()` additionally compares tag presence, length, and bytes
at every node. Clone uses this contract for reuse: renaming or reordering named
fields can require replacement even when their storage widths are identical.

Clone copies source tags but gives newly allocated nodes destination policy:
mutable, user-writable, update-resettable, and non-retentive. It never edits an
existing object's schema or permission flags in place. Existing matching
destinations must satisfy the applicable write policy before being refilled.

Copy/Clone allow eight pointer traversal levels with a scalar leaf at the end.
The ownership depth limit is separate: it bounds reclamation and linking, not
the size of a copy operation.

## Mutation and publication

Internal scalar, copy, and link APIs require `mutable` on each object they write.
The `_usr` APIs additionally reject `usr_protected`. SET and CLONE blocks use
the user APIs. Ordinary producer writes continue using the internal APIs.
`vm_obj_clear_quiet()` is an internal helper and also respects `mutable`.

The `usr_protected` bit occupies previously reserved bit 6 of the header's flags
byte. The header stays four bytes, and existing defined wire bits retain their
positions. The construction helper flag is `VM_OBJ_F_USR_PROTECTED` (bit 3 of the
helper's flag argument); it is not the raw serialized header byte.
Old descriptors with reserved bits zero remain user-writable when mutable.
Block construction marks its output objects and ENO protected automatically.
Protection is object-local: a pointer container's protection does not implicitly
protect separate child objects. A producer declaring protected child values must
mark those objects too.

Copy validates the complete destination before writing any values or update
flags. Type/count mismatch, missing children, depth failure, or denied writes
leave the destination unchanged. The subsequent commit cannot encounter a new
validation failure under the single-writer contract. This costs two traversals
and performs no object allocation. When graphs share value storage, Copy uses
deterministic slot-order writes; it is not a snapshot of every aliased source leaf.

Clone replacement follows **build → fill → validate link → publish → release**.
An allocation or validation failure releases the unpublished replacement and
preserves the previous destination. Filling before replacement also permits a
source located inside the tree being replaced. Matching destinations use Copy's
preflight and commit, and successful refill publishes the holder too.

Freshness has explicit scope:

| Operation | Objects marked `upd` |
| --- | --- |
| Scalar write | Object owning the addressed bytes |
| Changed pointer link | Object containing that slot |
| Copy | Destination root and traversed, written descendants |
| Clone | Filled destination and its pointer holder |
| `vm_obj_publish()` / `_usr()` | Explicitly selected aggregate |
| Quiet clear | None |

There is no automatic ancestor walk. Updating one field does not mark every
parent sharing it. A block publishing a complete message calls `vm_obj_publish()`
after its successful field writes. Pointer-slot freshness belongs to its container,
consistently for literal, cached, reference, and name accessors. Telemetry may scan
descendant flags independently; that does not alter block-trigger semantics.

## Validation

Self-test stage `OBJ` exercises ownership sharing/cycles/depth, tag-sensitive
reuse, descendant-source replacement, partial-allocation rollback, copy preflight,
write protection, explicit publication, overflowing indices, and program teardown.
Existing dynamic and cache tests use saved IDs or caller-owned accessor copies
when testing reclamation, rather than reading objects after their lifetime ends.

Graph validation is paid on dynamic link changes; arena-only linking skips it.
The copy preflight and schema comparisons add work that old benchmark figures
do not include. Re-measure on the target before using those figures for timing
budgets. No additional per-object or per-accessor storage is introduced.
