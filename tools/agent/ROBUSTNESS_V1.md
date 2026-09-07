# Agent API robustness checkpoint V1

The live V1 now implements bounded/revisioned pages, authoritative camera
context, screen-ray depth/object identification, atomic named checkpoints,
stable manifest keys, and explicit keep-clear validation. The deterministic
simulator remains an acceptance oracle, not a substitute for native integration
tests.

The native checkpoint deliberately exposes depth/object identity as an exact
pixel query rather than pretending that full-frame depth and ID PNG passes are
already synchronized. Full-frame auxiliary render passes and multiple
simultaneously visible scratch layers remain V2 work.

## Gaps found by the initial audit

- The bridge can set a camera but cannot read the user's live camera. Captures
  therefore lack authoritative position, target, up vector, FOV, aspect,
  viewport, projection, and scene revision metadata.
- RGB capture is the only render pass. There is no depth/object-ID pass and no
  screen pixel to world hit query. The renderer's existing colour-coded picking
  path is promising, but it is not exposed by the agent protocol.
- `list` is unbounded. `inspect_zone` is zoned and count-limited but not pageable;
  its `truncated` flag can also be true when exactly `limit` items exist. `validate`
  scans the complete scratch scene, builds unbounded JSON, and is quadratic for
  building overlap checks.
- The client permits a 60 KB request and receives up to 1 MB, while the transport
  is a single Unix datagram. On the audited macOS host a roughly 4.1 KB response
  failed with `EMSGSIZE`; a roughly 1.6 KB response succeeded. Count limits alone
  cannot make variable-size JSON safe.
- Scratch-session snapshots exist only in engine memory. A process restart loses
  the lease, rollback snapshot, native suppression lease, and session ID.
- Saving persists the active IPL but there are no named sidecar checkpoints,
  atomic manifests, or recovery metadata. Only one global agent document/session
  can be active.
- `ObjectInst::m_id` is allocated by a process-local monotonic counter. It is safe
  during one process lifetime but is not a durable identity after reload/restart.
- Validation understands support, slope, penetration, and coarse 2D building
  overlap. It does not model entrances, circulation routes, access clearance,
  ownership of a semantic zone, or prop intent.
- Capability output lists command names only. It does not advertise maximum
  request/response bytes, page size, supported render passes, identity stability,
  persistence semantics, or optional command versions.

## Required V1 contracts

### Camera and picking

- `camera_context` returns position, target/forward, up/right, vertical FOV,
  aspect, viewport dimensions, near/far planes, active camera kind, and the scene
  revision in one internally consistent response.
- `screen_to_world(pixel_x, pixel_y, viewport_width, viewport_height)` rejects a
  viewport that does not match the camera context and returns either a typed miss
  or world position, normal, depth, model ID, numeric `instance_id`, and durable
  `object_key`.
- RGB, linear depth, and object-ID passes are tied to the same camera context and
  scene revision. Stale pass reads fail rather than silently mixing frames.

### Bounded inspection and validation

- `list_page(offset, limit, x?, y?, radius?)`,
  `inspect_zone_page(x, y, radius, offset, limit)`, and
  `validate_zone(x, y, radius, offset, limit, details?)` filter before paging,
  use a deterministic order, and return `total`, `returned`, `next_offset`, and
  `scene_revision`.
- Every endpoint has both a count ceiling and serialized-byte ceiling. The bridge
  should shrink a page or return a structured `response_too_large` error before
  calling `sendto`.
- Mutation between offset pages is detectable through `scene_revision`. Callers
  restart enumeration when the revision differs.
- Validation broad phase is zone-limited. Detailed support samples are optional;
  summaries remain small enough for normal agent loops.

### Persistent layers and identity

- Each scratch layer has a durable random layer UUID and independent IPL path.
  Each created object receives an immutable `object_key` such as
  `<layer_uuid>:<sequence>`, alongside the process-local `instance_id`.
- Named checkpoints atomically persist layer manifest, canonical object state,
  stable keys, semantic annotations, source scene fingerprint, and format version.
- `checkpoint_save`, `checkpoint_list`, and `checkpoint_restore` live in the
  service/daemon. Restore maps durable keys to new process-local IDs and reports
  missing/ambiguous objects explicitly.
- Path traversal, partial writes, incompatible versions, and corrupt manifests
  fail closed. Temporary files are renamed only after fsync and validation.

### Minimal semantic validation

- V1 supports explicit semantic annotations rather than guessing from model
  names: entrance volumes, circulation corridors, keep-clear zones, and object
  roles.
- `validate_zone` reports stable issue codes, severity, involved object keys, and
  measured clearance. At minimum it detects objects blocking declared entrances
  or routes. Geometry-only support/overlap results remain separate issue groups.

## Acceptance gates

Run the deterministic simulator and full Python suite:

```sh
.venv-agent/bin/python tools/agent/simulation_harness.py
.venv-agent/bin/python -m unittest discover -s tools/agent/tests -v
```

The harness covers exact-once paging of 517 objects, a screen-centre world hit,
depth/object-ID querying, persistent stable keys across a simulated restart,
named checkpoint restore, semantic route blocking, request limits, and
response-budget failure. The live audit mode checks the same capability and
transport limits against a running Ariane socket.
