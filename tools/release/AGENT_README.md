# Ariane agent alpha — binary bundle

This archive contains an agent-enabled editor, its fonts, a Python wheel and
checksums. macOS also includes GLFW in `lib`. Keep the editor, `fonts` and `lib`
together. Supply your own GTA installation; no game assets are included.
Creative recipes currently target San Andreas. Python 3.10+ is required.

## Install the CLI and MCP

From this extracted directory on macOS/Linux:

```sh
python3 -m venv .venv-agent
.venv-agent/bin/python -m pip install './ariane_agent-0.1.0a1-py3-none-any.whl[mcp]'
.venv-agent/bin/arianectl assets index --gta-dir "/absolute/path/to/GTA San Andreas"
```

On Windows, first copy `ariane.exe` and the `fonts` directory into your GTA game
root (beside its game executable). Keep the wheel wherever convenient, then run
these commands in its directory with PowerShell:

```powershell
py -3 -m venv .venv-agent
.venv-agent\Scripts\python.exe -m pip install './ariane_agent-0.1.0a1-py3-none-any.whl[mcp]'
.venv-agent\Scripts\arianectl.exe assets index --gta-dir 'C:\Games\GTA San Andreas'
```

The basic offline index provides model IDs/names, textures, IDE sources, flags
and draw distances. It does not include dimensions, collision data, native IPL
usage or thumbnails; use live inspection and previews. An optional enriched
index can be built with `assets index --gtastuff "/path/to/gtastuff"`.
`ARIANE_ASSET_DB` or global `--db` selects a separate database for each game.

## Start the editor and check the connection

On macOS/Linux, launch with the GTA directory as the working directory:

```sh
cd "/absolute/path/to/GTA San Andreas"
"/absolute/path/to/extracted-bundle/ariane" --agent-socket /tmp/ariane-agent-v1.sock
```

In another terminal, run the installed CLI by its absolute path:

```sh
"/absolute/path/to/extracted-bundle/.venv-agent/bin/arianectl" ping
```

On Windows, run both editor and CLI from the SAME PowerShell session so they
inherit the same connection settings. Replace the CLI path with your install:

```powershell
$env:ARIANE_ENGINE_TCP_PORT = '48731'
$env:ARIANE_ENGINE_TOKEN = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
Start-Process 'C:\Games\GTA San Andreas\ariane.exe' -WorkingDirectory 'C:\Games\GTA San Andreas'
& 'C:\path\to\bundle\.venv-agent\Scripts\arianectl.exe' ping
```

Wait for the editor to finish loading before `ping`. TCP is authenticated and
binds only to loopback. A different CLI terminal or MCP host must receive the
same port and token (at least 32 characters). Unix sockets are private by default.

## First reversible edit (San Andreas)

Using the installed `arianectl` executable, the following sequence previews an
asset, opens a scratch layer and rolls the proposal back. Replace `/absolute/...`
with writable local output paths (Windows paths are accepted too):

```text
arianectl assets search bench --defined-only --limit 5
arianectl assets inspect 11490 --ensure-renderable
arianectl assets preview 11490 --output /absolute/path/to/preview.png
arianectl scene ariane/agent_scratch.ipl /absolute/path/to/agent-scratch.ipl
arianectl session begin first-proposal
arianectl place 11490 -800 1550 100 90
arianectl validate
arianectl capture-views /absolute/path/to/review
arianectl session rollback
```

Placement needs visual review and terrain validation; these coordinates are an
example, not an accepted placement. A proposal is retained with `session commit`
and written only by a separate `save` command. Rollback restores the snapshot.

## MCP configuration

Point your MCP host's stdio server command at the absolute installed
`.venv-agent/bin/ariane-mcp` (macOS/Linux) or
`.venv-agent\Scripts\ariane-mcp.exe` (Windows), with no arguments.
Set `ARIANE_ASSET_DB` if using a nondefault database. On Windows set
`ARIANE_ENGINE_TCP_PORT` and `ARIANE_ENGINE_TOKEN` to the editor's exact values.
On Unix, `ARIANE_ENGINE_SOCKET` can override `/tmp/ariane-agent-v1.sock`.

Use `arianectl --help` for commands. Full workflow and source build documentation:
https://github.com/Dryxio/ariane/tree/codex/agent-public-release/tools/agent

The Python wheel can also be installed without `[mcp]` for CLI-only use.
The legacy `ariane-agentd` Unix sidecar is optional and is not needed by MCP.

## Binary runtime requirements

Linux archives use an Ubuntu 22.04 glibc baseline and require system OpenGL and
GLFW. On Ubuntu/Debian install `sudo apt-get install libglfw3 libgl1` before
launching. A graphical display is required. Linux CI compiles the editor and
runs headless Python checks; this does not establish a real-game Linux runtime
validation.

macOS archives bundle GLFW. The alpha application is ad-hoc signed, not Apple
notarized. If Gatekeeper blocks a downloaded build you trust, use macOS System
Settings → Privacy & Security → Open Anyway after the first launch attempt.
