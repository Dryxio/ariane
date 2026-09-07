# Agent composition robustness V2

V2 removes the practical scale ceiling observed while composing the 105-object
Grove Street scene. The engine now serves length-prefixed JSON over a private
Unix stream. Requests are capped at 1 MiB, responses at 4 MiB, and pages at 256
items. Large mutation receipts therefore arrive intact instead of timing out
after the world was already changed.

The Python service adds the semantic layer an agent needs for large scenes:

- caller-owned object keys that survive service restarts and runtime-ID remaps;
- idempotent `patch_id` receipts and `expected_revision` preconditions;
- preflight validation plus compensating rollback for placement/transform
  failures;
- named groups with paged inspection, rigid transform, clone, and delete;
- explicit support graphs and collision-bound `snap_to_support`;
- composition validation that separates acknowledged stacked/tabletop props
  from unresolved float/embed warnings.

The CLI is the replay/debug surface and MCP is a typed adapter over the same
service. Neither owns separate editing logic.

## Acceptance gates

```sh
.venv-agent/bin/python tools/agent/simulation_harness.py
.venv-agent/bin/python -m unittest discover -s tools/agent/tests -v
```

The closed-loop suite exercises 613-object exact-once paging, a 150-object
single patch whose receipt exceeds the old datagram budget, idempotent replay,
stale-revision refusal, state persistence across service restart, group
transform/clone/delete, injected mid-patch failure compensation, support
snapping, semantic keep-clear checks, and the real MCP stdio tool schemas.

## Deliberate boundary

These are compensating sidecar transactions because the editor engine still
has no native begin/commit primitive for one compound patch. A process crash in
the middle of mutation cannot be made atomic by Python. The next hardening step,
if crash-atomic edits become necessary, is a native engine transaction journal;
normal tool errors are already compensated and tested.
