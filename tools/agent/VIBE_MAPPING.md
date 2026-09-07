# Vibe mapping workflow

The user supplies the idea in their own language and positions the Ariane camera
or selects objects. The external visual agent is the planner and art director.
CLI and MCP expose the same service; no hidden LLM, model downloads or fabricated
visual scoring are embedded in the editor.

## Run

```sh
PY=.venv-agent/bin/python
CLI=tools/agent/arianectl.py
$PY $CLI capabilities
$PY $CLI survey agent-output/site --radius 30
$PY $CLI call vibe.project.get
$PY $CLI call vibe.recipe.list
$PY $CLI environment
```

`survey` preserves the camera and writes current/annotated RGB, sampled depth and
object-ID visualizations, terrain normals, nearby native and scratch objects,
and `survey.json`. A selected image pixel can be specified with `--pixel-x` and
`--pixel-y` in the original viewport dimensions. The `selection` engine command
and `selected_mapping_objects` MCP tool expose editor selections.

Before mutation, create an isolated IPL destination and begin a scratch session
using the existing `scene` and `session begin` commands. Do not replace an active
user session just to run a demo.

## Creative loop

1. **Observe.** Read the project, selection and live camera. Survey the actual
   target. Visually identify access, available surfaces, surroundings and scale.
2. **Brief.** Preserve the original prompt in project `brief`. Normalize catalogue
   queries to English roles/styles/avoid; French user text stays in project memory.
   Store zones, functional relations, references and accepted choices.
3. **Palette.** Search catalogue and learned annotations. Render canonical asset
   views; reject wrong scale, styles or availability. Record observed materials,
   colours, fronts and local anchors with the image path as source and honest
   confidence. Unknown fronts remain unknown, never silently certified as +Y.
4. **Blockout.** Place the main forms and establish entrances/paths first. For an
   ambiguous direction, save two light named checkpoints and show actual views.
5. **Compose.** Build dimension-aware recipes where useful; otherwise use relative
   patches. Inspect recipe orientation assumptions. Copy generated circulation
   constraints into project zones. Resolve, inspect preflight, then apply the plan.
6. **Detail.** Work in meaningful named groups. Vary details deterministically and
   concentrate them where they have a plausible use. Avoid uniform prop scatter.
7. **Review.** Capture overview and player views with `vibe.review.capture`.
   Inspect the images and separately assess geometry, brief fidelity, coherence,
   proportions, circulation, repetition, details and preserved choices. Record
   specific object/group findings and the smallest corrective patch. Repeat until
   the result is acceptable; never treat a valid collision check as an art review.
8. **Retouch.** Read memory before every follow-up. Preserve locked keys/groups.
   Use `vibe.history.undo` for one edit and checkpoints for complete variants.
   Undo refuses to overwrite later object changes or dependent supported objects.
9. **Deliver.** Commit/save explicitly after review; export a review bundle if
   needed. A copied IPL is the last saved state, clearly distinct from live JSON.
   Test in-game through the editor's existing game-testing workflow.

## Shared methods

Call any method with `arianectl.py call METHOD --file params.json`; `--params`
also accepts a JSON object. The daemon uses the identical method/params pair.

| Method | Parameters |
|---|---|
| `vibe.project.get` | none |
| `vibe.project.update` | `changes`, optional `expected_revision` |
| `vibe.survey` | `output_directory`, optional radius/grid/pixel_x/pixel_y |
| `vibe.recipe.list` | none |
| `vibe.recipe.build` | name, palette, key, x, y; optional heading/count/aisle/gap/seed/points/gate_width |
| `vibe.history.list` | none |
| `vibe.history.undo` | `edit_id` |
| `vibe.variants.compare` | `first`, `second` checkpoint names |
| `vibe.route.check` | ground-level xyz `points`, optional width/height |
| `vibe.review.capture` | `output_directory`, optional center/span |
| `vibe.review.record` | `review_path`, scores, findings, optional accepted |
| `vibe.export` | `output_directory` |
| `profiles.get` | `asset_id` |
| `profiles.annotate` | asset_id, features, source, optional confidence |
| `profiles.search` | query, optional limit |
| `profiles.index_images` | local `directory` containing MODEL_ID.png |
| `profiles.reference_search` | image_path, optional limit/candidates |
| `scene.environment` | optional hour/minute/weather_a/weather_b/blend |
| `scene.suppress_native` | x/y/radius/model_ids |

All have named MCP counterparts. SceneOperation now accepts `transform3d` with
pitch, roll and heading; the remaining coordinates are explicit. Existing
low-level engine/legacy file transports remain compatibility paths. Project
locks are service-level protections, not a security boundary against editing the
map manually or calling the raw engine socket.

### Project document

Lists replace their previous values. Use `expected_revision` when changing a
previously read project. Supported fields: brief, styles, avoid, zones, relations,
palette, references, locked_keys, locked_groups, notes, phase. Phases: observe,
palette, blockout, compose, detail, review, accepted. Relations are agent-authored
intent; only explicit geometric zones and support relationships are machine
validated. A group lock protects its existing members and additions via patches.

Recipe palettes: terrace(table, seat), market(counter, crate), checkpoint(barrier),
alley(clutter), fence(fence). Fence points are local xy coordinates relative to
x/y/heading; gate_width leaves a central opening on each segment. Recipes use
collision dimensions and compensate model origins. Their fronts require visual
review, especially for assets lacking annotations.

## Verification and honest limits

```sh
$PY -m unittest discover -s tools/agent/tests -v
make -C build euryopa config=release_macos-arm64-gl3
$PY tools/agent/vibe_benchmark.py init agent-output/vibe-benchmark
$PY tools/agent/vibe_benchmark.py report agent-output/vibe-benchmark
```

The benchmark has 20 briefs plus follow-up edits. Each needs real initial and
retouch review artifacts. Missing/unreviewed trials stay missing; preparation is
not a benchmark pass. Compare human preference, time to acceptance, corrections,
geometry and independently recorded visual scores.

Reference search currently uses HSV histograms to shortlist colours. It is not
semantic image embedding retrieval; the external visual agent must interpret the
reference and judge candidate images. The profile store supports learned
functional anchors but the entire catalogue has not been visually annotated.
Screen/depth maps are sampled rays. Route checks use six rays per segment and do
not prove navmesh connectivity or walkable ground. Collision validation remains
an approximation, with oriented footprint rejection reducing some AABB false
positives. Pitch/roll placements still require a live post-apply support check.
History is a sidecar journal with compensating edits, not native crash-atomic
transactions. There is no embedded conversational UI: prompting happens through
the connected visual agent, with Ariane's camera/selection as spatial input.

### Live verification on 2026-09-04

Built and launched `ariane-fnv1a64-cdf97638362bd0c5` against the local San Andreas
installation. Verified camera-target survey, annotated RGB, sampled depth/IDs,
terrain samples, selection and reversible hour/weather changes. A disposable
six-object terrace using models 2111/1810 passed native placement preflight and
post-apply validation; four review views rendered, selective undo removed all six
objects and the session was rolled back. The live camera pose was preserved.

Visual inspection caught and fixed inverted asset PNGs: the PNG path now uses
world +Z up, while the UI thumbnail rendering retains its existing convention.
The two assets were visually inspected after recompilation. Functional fronts
remain unannotated until separately verified. Observed material/role annotations
and corrected previews are persisted in the local agent cache/state.

Named functional anchors can be used by `place_relative` through `parent_anchor`
and `child_anchor`. Anchors are model-local xyz; use `snap: false` to retain their
vertical alignment. The parent must precede the child. Unknown anchors fail
rather than falling back to guessed geometry.

## Creative workflow V2 (2026-09-04)

The CLI `call` interface and typed MCP tools expose the same implementation:

- `catalogue.search`: French/English concept vocabulary, role eligibility, dimensions
  `[width, depth, height]`, style preferences, hard exclusions and ranking evidence.
- `catalogue.palette`: role-grouped candidates and explicit missing roles. It reuses
  one catalogue snapshot per request. A missing role is never filled by an unrelated prop.
- `catalogue.board`: labelled two-view native previews, dimensions and per-model
  preview failures; successful files remain available when another model fails.
- `profiles.anchors`: measured `bounds.base`, `bounds.top`, `bounds.plus_y`, etc.,
  separated from sourced functional anchors. Bounds do **not** identify doors.
- `vibe.recipe.variants`: compact/balanced/open patches with circulation constraints;
  no scene or project mutation. Options include `key`, `x`, `y`, `heading`, `count`.
- `rig.create`, `rig.capture`, `rig.compare`: freeze existing capture poses and compare
  two or three revisions. Comparison requires the same scene, rig and atmosphere.

24 bundled passports have native visual evidence in
`reference-data/native-assets-20260904.png`. Local annotations override bundled
passports. Enriching a passport preserves existing fields and records each field's
source. Functional orientation remains unknown until inspected and annotated.
External semantic descriptions are excluded from creative retrieval by default:
real catalogue entries describe a shipping container as a concrete ramp. They also
cannot authorize shelter/collision exceptions. Explicit observed roles override
inferences from model names (a standalone parasol is not seating).

This is **concept/annotation retrieval**, not a neural text-image embedding index.
Reference image matching still uses HSV histograms and requires visual review.

### Placement example

```json
{
  "action": "place_relative", "key": "stock.upper", "model": 964,
  "relative_to": "stock.lower", "parent_anchor": "bounds.top",
  "child_anchor": "bounds.base", "supported_by": "stock.lower", "snap": false
}
```

For `in_front`, `behind`, `left`, and `right`, footprints account for both model
rotations and off-centre origins. `functional_front: true` requires an observed
parent `front_heading`. `face_towards: [x,y]` (or an existing object key) requires
an observed child front. Conflicting bbox-relative/anchor-facing requests and
tilted parents are rejected rather than approximated silently.

Recipes now include `camp`, `logistics`, `outpost`, `garden` alongside `terrace`,
`market`, `checkpoint`, `alley`, `fence`. Outposts have one guard and one landmark;
logistics use one generator; gardens form seating/planting pockets. Circulation
uses conservative extents under rotation. Markets retain a small margin against
float rounding at the aisle boundary. Merge returned constraints into project
zones **before resolving** a patch. Recipes remain editable blockouts: inspect
fronts, local access, native context and visual density before decorating.

### Reproducible checks

```sh
PYTHONDONTWRITEBYTECODE=1 .venv-agent/bin/python -m unittest discover -s tools/agent/tests -v
PYTHONDONTWRITEBYTECODE=1 .venv-agent/bin/python tools/agent/creative_harness.py /tmp/ariane-creative-trials
```

The native harness requires an empty engine and refuses to replace an open user
map. It runs six palettes (military, beach, industrial, rural, garden, market),
two layouts each, placement preflight, final validation, fixed-view comparison,
checkpoint and selective undo. Terrain trials include flat ground, slopes,
shoreline discontinuities and missing ground. All mutations target its disposable
layer and are rolled back; the camera and prior atmosphere are preserved.

These are technical smoke scenes on the same flat test site, **not six finished
art-directed maps**. The separate `vibe_benchmark.py` still needs completed visual
brief/follow-up reviews to establish aesthetic performance. No navmesh validation,
automatic door recognition, full mesh-aware interior packing or universal terrain
success is claimed. A robust outcome can be an actionable refusal to place.
