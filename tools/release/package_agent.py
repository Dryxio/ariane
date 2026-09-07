#!/usr/bin/env python3
"""Package an agent engine and Python wheel without local/game data."""
import argparse
import hashlib
from pathlib import Path
import platform
import shutil
import subprocess
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--engine', required=True, type=Path)
p.add_argument('--wheel', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--name', required=True)
p.add_argument('--glfw', type=Path, help='macOS GLFW shared library to bundle')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
stage = a.output / a.name
stage.mkdir(exist_ok=False)
engine = stage / a.engine.name
shutil.copy2(a.engine, engine)
shutil.copy2(a.wheel, stage / a.wheel.name)
shutil.copytree(root / 'fonts', stage / 'fonts', ignore=shutil.ignore_patterns('.DS_Store'))
shutil.copy2(root / 'tools/release/AGENT_README.md', stage / 'README.md')
if a.glfw:
    lib = stage / 'lib'; lib.mkdir()
    bundled = lib / 'libglfw.3.dylib'
    shutil.copy2(a.glfw.resolve(), bundled)
    deps = subprocess.check_output(['otool', '-L', str(engine)], text=True)
    glfw = next(line.strip().split(' (')[0] for line in deps.splitlines() if 'libglfw' in line)
    subprocess.run(['install_name_tool', '-change', glfw, '@executable_path/lib/libglfw.3.dylib', str(engine)], check=True)
    subprocess.run(['install_name_tool', '-id', '@loader_path/libglfw.3.dylib', str(bundled)], check=True)
    for f in [bundled, engine]:
        subprocess.run(['codesign', '--force', '--sign', '-', str(f)], check=True)
    deps = subprocess.check_output(['otool', '-L', str(engine)], text=True)
    if '/opt/homebrew' in deps or '/usr/local' in deps:
        raise SystemExit('Unbundled non-system dependency in engine')
files = sorted(f for f in stage.rglob('*') if f.is_file())
(stage / 'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.relative_to(stage).as_posix()+'\n' for f in files))
archive = a.output / (a.name + '.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file(): z.write(f, f.relative_to(a.output))
print(archive)
