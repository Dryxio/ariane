# Ariane MCP reliability checkpoint

This is one coherent compatibility checkpoint for testing the agent feedback.
It keeps the older MCP and CLI tools while adding the safer contracts below.

## What changed

- Captures are MCP-native. `capture_current_view` preserves the live viewport;
  `render_scene_views` renders temporary poses and restores the complete camera
  only when its revision is still safe to restore. Every image reports its
  actual pose, scene revision, camera revision, and label. Empty scratch scenes
  can be surveyed with an explicit center and span.
- `capture_from_pose` exposes the underlying revision-safe arbitrary-pose capture
  directly to MCP. Pose provenance now distinguishes the actual orthonormal
  rendered basis from the supplied up reference.
- Empty-scene survey framing tests nine target-footprint rays against world
  collision and chooses among deterministic orbit/height/distance candidates.
  The manifest preserves visibility scores, blockers, and pose adjustments.
- Asset inspection now fuses catalogue metadata with runtime truth:
  `catalog_only`, `defined_unloaded`, `renderable`, or `load_failed`. Collision
  bounds explicitly use model-local coordinates relative to the origin. The
  identity axes are documented, and `render_asset_views` produces two local
  canonical views instead of relying on remote thumbnails.
- Patch resolution is read-only and revision-bound. `resolve_scene_patch`
  prevalidates every model, resolves optional snapped Z, support stacking,
  relative placement, and deterministic line/ring arrays. Its hypothetical
  validator covers support, duplicates, and overlaps before mutation;
  `apply_resolved_plan` refuses an invalid, stale-session, or stale-revision plan.
- Patch errors identify the operation index, action, key, model, current
  revision, and whether compensation succeeded. Runtime revisions stay
  monotonic and are no longer discarded by the Python exception layer.
- `validate_scene` and `validate_composition` now use the same composition-aware
  validator. It recognizes valid scratch-object support while rejecting bad
  support declarations, exact duplicates, building collisions, and obvious
  small-prop overlaps. Containment under a classified canopy, awning, gazebo,
  parasol, umbrella, pergola, or archway is retained as an informational finding.
- Semantic descriptions that contradict measured scale are flagged rather than
  silently trusted. `discover_assets(thoroughness=quick|broad|exhaustive)` gives
  agents one obvious entry point while retaining the detailed discovery tools.
- Terrain support now has one `terrain-support-v1` contract shared by resolve,
  fit, analyze, and validate. Resolve reports footprint support before mutation,
  invalid plans fail closed, and a no-op fit does not advance scene revision.
- Capabilities expose an exact per-compilation `build_id`. Quick discovery is
  compact and concept-balanced. Survey probes never target below support and
  named compass views are clamped to a 30-degree auto-orbit.

## Automated verification

From the repository root:

```sh
.venv-agent/bin/python -m unittest discover -s tools/agent/tests -v
make -C build euryopa config=release_macos-arm64-gl3
```

## Live acceptance pass

Start the newly built Ariane binary with the agent socket, open an agent scene,
and begin a scratch session. Then run these from the repository root:

```sh
PY=.venv-agent/bin/python
CLI=tools/agent/arianectl.py

$PY $CLI ping
$PY $CLI capabilities
$PY $CLI assets search "speaker stall table" --defined-only --limit 12
$PY $CLI assets inspect 19831 --ensure-renderable
$PY $CLI assets preview 19831 --output agent-output/checkpoint-v3/asset-19831.png
$PY $CLI capture agent-output/checkpoint-v3/current.png --label before-edit
$PY $CLI capture-views agent-output/checkpoint-v3/survey \
  --center 0 0 0 --span 80
```

Replace the survey center with the map location under review. Confirm that the
user viewport is identical before and after both capture commands and inspect
`survey/capture-manifest.json` for four correctly labelled poses.

Create `agent-output/checkpoint-v3/patch.json` with known renderable model IDs:

```json
{
  "operations": [
    {"action":"place","key":"base","model":1234,"x":0,"y":0,"heading":0,"snap":true},
    {"action":"place","key":"top","model":1235,"x":0,"y":0,"heading":0,
     "snap":true,"supported_by":"base","support_mode":"snap","clearance":0.01},
    {"action":"array_line","key":"lights","model":1236,
     "x":-6,"y":4,"x2":6,"y2":4,"count":5,"heading":180,"snap":true,"seed":"acceptance"}
  ]
}
```

Then resolve, inspect, apply, and validate:

```sh
$PY $CLI resolve-patch agent-output/checkpoint-v3/patch.json
$PY $CLI apply-plan PLAN_ID
$PY $CLI validate-composition
```

Expected results: resolving does not change `scene_revision`; applying changes
it and creates eight objects; the top object rests on `base`; validation does
not report that supported object as floating. Reapplying the same plan returns
the replay receipt. Editing the scene after resolve but before apply must return
`code: stale_revision` without applying any planned operation.

Finally, replace one model ID with a catalogue-only/unknown ID and resolve again.
It must fail with `code: asset_unavailable`, the exact `operation_index`, key and
model, and no scene mutation.
