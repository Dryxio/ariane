# Ariane agent alpha

The current creative workflow is documented in [VIBE_MAPPING.md](VIBE_MAPPING.md):
camera/selection surveys, persistent briefs and locks, dimension-aware recipes,
sourced asset annotations, selective undo, variants, atmosphere and visual review.

This checkpoint turns Ariane into a local, agent-controllable map editor without
putting an LLM or MCP implementation inside the C++ application:

```text
agent -> MCP stdio / arianectl -> shared Python service -> private local IPC -> Ariane main thread
                                      |
                                      +-> local SQLite catalogue + optional thumbnails
```

The CLI is the canonical, replayable interface. MCP is a typed adapter over the
same service. `ariane-agentd` is an optional persistent JSON-lines endpoint for
integrations that do not want to own the Python service process.

## One-time setup

Requires Python 3.10 or newer and your own supported GTA game installation.
No GTA models, textures or other game assets are included. The creative recipes
and reviewed annotations currently target San Andreas.

Install from the repository root (the MCP extra adds the stdio server):

```sh
python3 -m venv .venv-agent
.venv-agent/bin/python -m pip install '.[mcp]'
.venv-agent/bin/arianectl assets index --gta-dir "/absolute/path/to/GTA San Andreas"
```

On Windows PowerShell use `py -3 -m venv .venv-agent`, then
`.venv-agent\Scripts\python.exe -m pip install ".[mcp]"` and
`.venv-agent\Scripts\arianectl.exe assets index --gta-dir "C:\Games\GTA San Andreas"`.
Install `.` without the extra if only the CLI is needed. Installation includes
the Python modules and reviewed annotation resource; it does not compile the
editor. Build or download an agent-enabled editor from the same revision.

The basic catalogue is built offline from object definitions in your game's
`data` directory. It provides IDs, model/texture names, IDE sources, flags and
draw distances. Names supply weak search hints. Dimensions, collision availability,
native IPL usage and thumbnails are not extracted by this basic importer;
use live `assets inspect ID --ensure-renderable` and `assets preview ID` for
runtime checks. `--has-collision` and dimension filters need an enriched index.
All IDE files under `data` are scanned, so `--defined-only` checks which entries
are actually defined in the running editor. Rebuild when changing games or mods;
use a separate `--db` for each installation because model IDs overlap.

An optional existing gtastuff checkout supplies richer geometry, collision,
IPL and thumbnail metadata:

```sh
.venv-agent/bin/arianectl assets index --gtastuff "/absolute/path/to/gtastuff"
```

It is not needed for the basic installation. Optional `--semantic-assets`
accepts an external caption dataset; no such dataset is bundled. Captions remain
recall hints, never authoritative functional labels. `ARIANE_ASSET_DB` overrides
the default `~/.cache/ariane-agent/assets-v1.sqlite3`; `--db` takes precedence.
`ARIANE_GTASTUFF` optionally supplies the enriched catalogue source.

There is deliberately no Ollama, embedding daemon, or local model dependency.
Codex observes the scene, turns the request into explicit roles/styles/negative
guidance, and visually judges labeled GTA thumbnails. SQLite handles exhaustive
catalogue navigation and auditable coverage.

## Codex-first asset discovery

For a scene-aware search, start a persistent discovery session rather than
trusting one semantic top-k:

```sh
PY=.venv-agent/bin/python
CLI=tools/agent/arianectl.py

$PY $CLI discovery start "outdoor guest area at a desert motel" \
  --role seating --role table --role cooking_fire --role waste --role lighting \
  --style desert --style weathered \
  --avoid interior --avoid weapon --avoid building
$PY $CLI discovery coverage DISCOVERY_ID
$PY $CLI discovery atlas DISCOVERY_ID \
  --output agent-output/desert-palette.png --family-limit 32 \
  --thumbnail-dir /path/to/gtastuff/thumbnails
$PY $CLI discovery families DISCOVERY_ID --limit 100
$PY $CLI discovery open DISCOVERY_ID FAMILY_NAME
$PY $CLI discovery residuals DISCOVERY_ID --limit 64
```

Each run considers every eligible non-LOD asset, fuses lexical captions, stable
weak labels, native IPL context, and deterministic family exploration, then
retains at least one representative of every family when the pool permits it.
The first atlas is balanced across requested roles. Opening a family pages every
catalogue member, including models no ranker selected. Residual audits sample
outside the pool, and shortlist/rejection decisions remain in the session ledger.

Run the repeatable closed-loop recall/stability benchmark with:

```sh
$PY tools/agent/discovery_harness.py \
  --output agent-output/discovery-harness \
  --iterations 5 --pool-limit 2500 --render-atlases \
  --thumbnail-dir /path/to/gtastuff/thumbnails
$PY -m unittest discover -s tools/agent/tests -v
```

## Launch and smoke test

Launch the agent-enabled Ariane build from the GTA directory so the game files
resolve normally:

```sh
cd "/absolute/path/to/GTA San Andreas"
"/absolute/path/to/ariane" --agent-socket /tmp/ariane-agent-v1.sock
```

In another terminal, from this repository:

```sh
PY=.venv-agent/bin/python
CLI=tools/agent/arianectl.py

$PY $CLI ping
$PY $CLI assets search "western saloon" --defined-only --limit 12
$PY $CLI assets inspect 11490 --ensure-renderable
$PY $CLI assets preview 11490 --output agent-output/11490-orientation.png
$PY $CLI assets contact-sheet 3249 11490 11501 11502 11503 \
  --output agent-output/western-shortlist.png

$PY $CLI scene 'ariane\agent_scratch.ipl' "$PWD/agent-output/agent-scratch.ipl"
$PY $CLI session begin first-proposal
$PY $CLI place 11490 -800 1550 100 90
$PY $CLI validate
$PY $CLI capture-views "$PWD/agent-output/review-first-proposal"
$PY $CLI capture-views "$PWD/agent-output/empty-survey" \
  --center -800 1550 100 --span 80
$PY $CLI session rollback
```

For geometry-aware placement, inspect signed support at the footprint and use
the appropriate profile. Buildings remain upright and are moved to sufficiently
flat nearby support; props can receive full pitch/roll alignment:

```sh
$PY $CLI analyze-placement INSTANCE_ID
$PY $CLI fit-terrain INSTANCE_ID building --radius 30 --step 5 --max-relief 0.6
$PY $CLI fit-terrain INSTANCE_ID prop --max-relief 0.4
```

The analysis returns nine transformed collision support samples, terrain height
and normal, signed clearance, support relief, slope, and a no-float Z.
Validation rejects floating corners, excessive penetration, missing terrain,
and footprints spanning unsupported relief. Resolve, fit, analyze, and validate
all report the same `terrain-support-v1` contract. A resolved plan contains a
preflight summary covering support, exact duplicates, building overlaps, and
prop overlaps, and an invalid plan cannot be applied. High-containment pairs
under assets classified as shelters are reported as informational rather than
blocking ordinary under-canopy composition.

For visual-agent loops, read the live viewport and resolve interesting screenshot
pixels instead of guessing world coordinates:

```sh
$PY $CLI camera-context
$PY $CLI screen-to-world PIXEL_X PIXEL_Y VIEWPORT_WIDTH VIEWPORT_HEIGHT
$PY $CLI inspect-zone X Y RADIUS --offset 0 --limit 8
$PY $CLI validate-zone X Y RADIUS --offset 0 --limit 8
```

Bulk replies use a length-prefixed local Unix stream, are revisioned, and remain
bounded to 4 MiB and 256 items. The service restarts enumeration if the scene
changes between pages. The old datagram bridge is accepted only as a temporary
client-side deployment fallback; new builds no longer risk applying a large
batch and losing its returned IDs.

For replay-safe composition, use caller-owned keys and a patch ID. Groups are
first-class sidecar metadata, so a whole cluster can be inspected, transformed,
cloned, or deleted without issuing fragile ID-by-ID instructions. Explicit
support relations distinguish intentional tabletop/stacked props from true
floating objects:

```sh
$PY $CLI patch agent-output/lounge-patch.json
$PY $CLI groups list
$PY $CLI groups transform lounge --dx 4 --dy -2 --dheading 15
$PY $CLI support snap bottle table --clearance 0.01
$PY $CLI validate-composition
```

Patch files contain either an operations array or
`{"patch_id":"...","expected_revision":42,"operations":[...]}`. A `place`
operation requires a stable `key`; replaying the same patch ID returns its prior
receipt without duplicating anything.

Resolve first when a proposal contains terrain snapping, relative placement,
support stacking, or line/ring arrays. Resolution loads and verifies every model,
expands authoring macros, computes final coordinates, and writes a plan bound to
the current scratch session and scene revision. It does not mutate the scene:

```sh
$PY $CLI resolve-patch agent-output/lounge-patch.json
$PY $CLI apply-plan PLAN_ID
```

`z` may be omitted when `snap` is true. `supported_by` records intent; add
`"support_mode":"snap"` to place the child on the parent's collision top.
`place_relative` resolves local offsets using the parent's heading; its optional
`relation` can be `on_top`, `in_front`, `behind`, `left`, or `right`, with
collision bounds plus `gap` determining separation. `array_line` and
`array_ring` expand deterministically and accept `jitter`, `heading_noise`, and
`seed`. Patch failures return `operation_index`, `action`, `key`, `model`,
`current_revision`, and rollback status where applicable.

Named checkpoints survive an Ariane restart without saving the IPL. Restore
requires an active scratch session and returns the new runtime ID corresponding
to each durable manifest key:

```sh
$PY $CLI checkpoint save reviewed-a
$PY $CLI checkpoint list
$PY $CLI session begin restore-reviewed-a
$PY $CLI checkpoint restore reviewed-a
```

Entrances and paths are validated from explicit world-space constraints rather
than unreliable model-name guesses. Pass a JSON array of circle/rectangle zones
to `validate-semantics`; the equivalent typed MCP tool is
`validate_semantic_zones`.

To retain a reviewed proposal, start another session, edit it, then explicitly
commit and save:

```sh
$PY $CLI session begin accepted-proposal
$PY $CLI place 11490 -800 1550 100 90
$PY $CLI session commit
$PY $CLI save
```

Mutations are refused outside an active scratch session. While a session is
active, save is refused. Rollback restores the snapshot taken at session start,
including transforms and deletions. Commit only accepts the visible proposal;
save remains a separate explicit operation.

`batch` accepts tab-separated rows containing
`model, x, y, z, heading, snap`. `capture-views` creates aerial, south, west,
and player-height images without leaving the live viewport moved. It also writes
`capture-manifest.json`, including the actual pose and camera revision for every
image. An explicit `--center X Y Z --span N` enables survey before any scratch
object exists. `capture-pose` provides the same conditional restore contract for
one arbitrary pose.

The MCP equivalent, `capture_from_pose`, accepts an exact position, target, up
reference, FOV, label, and optional expected camera revision. Captures report
the rendered `forward/right/up` basis separately from `up_reference`.
Multi-view surveys score up to nine line-of-sight probes across the target
footprint. A lower probe is omitted when it would fall below the support plane,
and named compass views never orbit by more than 30 degrees. Surveys record the
selected pose, omitted probes, occluders, visibility score, and any auto-orbit
in the manifest. The review-sheet caption explicitly marks an adjusted view.

`ariane_status`/`capabilities` includes a per-compilation `build_id`. Compare it
before every acceptance pass so a stale process cannot be mistaken for the
binary that was just deployed. `discover_assets` splits enumerated briefs into
independent concepts, merges candidates round-robin, and returns a compact
projection by default; request `detail="full"` only when full passports are
actually needed.

## MCP

Point an MCP host at the stdio server:

```json
{
  "mcpServers": {
    "ariane": {
      "command": "/absolute/path/to/ariane/.venv-agent/bin/ariane-mcp",
      "args": [],
      "env": {
        "ARIANE_ENGINE_SOCKET": "/tmp/ariane-agent-v1.sock",
        "ARIANE_ASSET_DB": "/absolute/path/to/assets-v1.sqlite3"
      }
    }
  }
}
```

The exposed tools cover runtime-filtered asset search, local canonical previews,
simple or exhaustive discovery, scene inspection, scratch sessions, resolved
typed patches, unified validation, provenance-bound captures, commit/rollback,
and save. MCP runs over stdio; Ariane itself exposes
a user-private Unix socket (`0600`) by default on macOS/Linux. Windows uses
the authenticated loopback TCP transport described below.

For a persistent local sidecar instead, run:

```sh
.venv-agent/bin/python tools/agent/ariane_agentd.py
```

It listens on `/tmp/ariane-agentd-v1.sock`, also mode `0600`, and accepts one
JSON request per line using the shared service methods. The old
`ariane_agent.py --bridge` file transport remains only as a compatibility path
for the initial prototype.

## Windows engine connection

Windows requires the agent-enabled Windows build. Copy `ariane.exe` and its
`fonts` directory into the GTA game root; Windows resolves game paths relative
to the executable. In PowerShell, configure the
same two environment variables for the editor process and each CLI/MCP process:

```powershell
$env:ARIANE_ENGINE_TCP_PORT = "48731"
# Generate once, then copy this value securely into the other local terminal/host.
$env:ARIANE_ENGINE_TOKEN = [guid]::NewGuid().ToString("N") + [guid]::NewGuid().ToString("N")
Set-Location "C:\Games\GTA San Andreas"
& ".\ariane.exe"
```

Use a token of at least 32 characters. For MCP, set the same variables in the
host's `env` object and use the absolute `.venv-agent\Scripts\ariane-mcp.exe`
command. This transport binds only to loopback and authenticates every request.
The optional legacy `ariane-agentd` Unix sidecar is for macOS/Linux; the CLI and
MCP connect directly to the engine on Windows.

## Headless checks

```sh
.venv-agent/bin/python -m unittest discover -s tools/agent/tests -v
.venv-agent/bin/arianectl --help
```

These checks exercise the Python service and simulated transport. A real GTA
installation, graphical editor and matching binary are still required to verify
rendering, placement, captures and save behavior. Advanced workflows below the
quick start may depend on enriched metadata and San Andreas model IDs.

## Binary runtime requirements

Linux archives use an Ubuntu 22.04 glibc baseline and require system OpenGL and
GLFW. On Ubuntu/Debian install `sudo apt-get install libglfw3 libgl1` before
launching. A graphical display is required. Linux CI compiles the editor and
runs headless Python checks; this does not establish a real-game Linux runtime
validation.

macOS archives bundle GLFW. The alpha application is ad-hoc signed, not Apple
notarized. If Gatekeeper blocks a downloaded build you trust, use macOS System
Settings → Privacy & Security → Open Anyway after the first launch attempt.
