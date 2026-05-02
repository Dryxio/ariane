ariane
======

Join the Discord for download, latest updates, progress etc..
Discord : https://discord.gg/8NS59AbQtN

A map editor for GTA III, Vice City and San Andreas, built on [librw](https://github.com/aap/librw).

Forked from [euryopa](https://github.com/aap/librwgta) by aap.

## Features

- Map viewer and editor for GTA III, Vice City and San Andreas
- Place, move, rotate and delete map objects
- Object browser
- Undo/redo support
- Save changes back to IPL files and IMG archives
- Day/night cycle and weather control

## Building

Requires [librw](https://github.com/aap/librw) built from source. Set the `LIBRW` environment variable to point to your librw directory, then use premake5 to generate build files.

```bash
cd build
# macOS ARM64
make config=release_macos-arm64-gl3 euryopa
# macOS x86_64
make config=release_macos-amd64-gl3 euryopa
# Windows (D3D9)
make config=release_win-amd64-d3d9 euryopa
```

## Usage

Run from a GTA game directory (III, VC or SA). Ariane auto-detects the game version.

## License

Since this project depends on LZO (GPL), consider the code in this repo dual-licensed as GPL.

## Credits

- [aap](https://github.com/aap) - original euryopa/librwgta project
