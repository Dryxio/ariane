ariane
======

Join the Discord for download, latest updates, progress etc..
Discord : https://discord.gg/8NS59AbQtN

A map editor for GTA III, Vice City and San Andreas, built on the [`ariane` integration branch of Southland-FR/librw](https://github.com/Southland-FR/librw/tree/ariane).

Forked from [euryopa](https://github.com/aap/librwgta) by aap.

## Features

- Map viewer and editor for GTA III, Vice City and San Andreas
- Place, move, rotate and delete map objects
- Object browser
- Undo/redo support
- Save changes back to IPL files and IMG archives
- Day/night cycle and weather control

## Building

Set `LIBRW` to the dedicated `librw-ariane` worktree. For reproducible builds,
use the exact commit recorded as `LIBRW_REF` in
`.github/workflows/build-euryopa.yml`, currently
`15ffa585216a9a7573ecc597b19ce2fde9b935f2`. Build librw before generating
Ariane with the PE channel.

```bash
export LIBRW=/path/to/librw-ariane
(cd "$LIBRW" && premake5 gmake2 --gfxlib=glfw)
make -C "$LIBRW/build" config=release_macos-arm64-gl3 librw

premake5 gmake2 --channel=PE
make -C build config=release_macos-arm64-gl3 euryopa
```

On Windows, run from a Visual Studio developer shell:

```bat
set LIBRW=C:\path\to\librw-ariane
pushd %LIBRW%
premake5 vs2019
msbuild build\librw.sln /p:Configuration=Release /p:Platform=win-amd64-d3d9 /t:librw /m
popd

premake5 vs2019 --channel=PE
msbuild build\librwgta.sln /p:Configuration=Release /p:Platform=win-amd64-d3d9 /t:librwgta;euryopa /m
```

## Usage

Run from a GTA game directory (III, VC or SA). Ariane auto-detects the game version.

## License

Since this project depends on LZO (GPL), consider the code in this repo dual-licensed as GPL.

## Credits

- [aap](https://github.com/aap) - original euryopa/librwgta project
