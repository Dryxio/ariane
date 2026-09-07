#!/usr/bin/env python3
"""Opt-in SA runtime acceptance test. Supply an isolated COPY of your game."""
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--game-copy', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--tcp', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['ARIANE_AGENT_STATE_DIR'] = str(output / 'state')
    env['ARIANE_ASSET_DB'] = str(output / 'assets.sqlite3')
    env['ARIANE_DISCOVERY_DIR'] = str(output / 'discoveries')
    engine = args.engine.resolve()
    if os.name == 'nt':
        deployed = args.game_copy.resolve() / 'ariane-agent-smoke.exe'
        shutil.copy2(engine, deployed)
        engine = deployed
    engine_args = [str(engine)]
    if args.tcp or os.name == 'nt':
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0)); port = reserve.getsockname()[1]
        env['ARIANE_ENGINE_TCP_PORT'] = str(port)
        env['ARIANE_ENGINE_TOKEN'] = secrets.token_hex(32)
    else:
        env.pop('ARIANE_ENGINE_TCP_PORT', None)
        env.pop('ARIANE_ENGINE_TOKEN', None)
        env['ARIANE_ENGINE_SOCKET'] = str(Path('/tmp') / ('ariane-smoke-'+secrets.token_hex(5)+'.sock'))
        engine_args += ['--agent-socket', env['ARIANE_ENGINE_SOCKET']]
    cli = [sys.executable, '-m', 'ariane_agent_tools.arianectl']
    def call(*fields):
        result = subprocess.run(cli + list(map(str, fields)), env=env, text=True, capture_output=True, timeout=45, cwd=output)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return json.loads(result.stdout)
    report = {'checks': [], 'platform': sys.platform}
    with (output / 'engine.log').open('w') as log:
        process = subprocess.Popen(engine_args, cwd=args.game_copy.resolve(), env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 90
            while True:
                if process.poll() is not None:
                    raise RuntimeError('Engine exited before ready; see engine.log')
                try:
                    assert call('ping')['result'] == 'pong'; break
                except (RuntimeError, subprocess.TimeoutExpired):
                    if time.monotonic() > deadline: raise
                    time.sleep(.5)
            report['checks'].append('startup and CLI ping')
            report['capabilities'] = call('capabilities')
            call('assets', 'index', '--gta-dir', args.game_copy.resolve())
            assert call('assets', 'search', 'bench', '--limit', 3)
            report['checks'].append('offline GTA catalogue indexing and search')
            call('camera', 2490, -1690, 25, 2490, -1665, 14, 58)
            pose = call('camera-context')['camera']['position']
            call('capture', output / 'current view.png')
            call('capture-pose', output / 'temporary view.png', 2490, -1695, 30, 2490, -1665, 14)
            assert call('camera-context')['camera']['position'] == pose
            from PIL import Image, ImageStat
            with Image.open(output / 'current view.png') as image:
                assert image.width >= 640 and max(ImageStat.Stat(image.convert('RGB')).stddev) > 5
            report['checks'].append('camera, rendered PNG, temporary capture restores pose')
            call('scene', 'ariane\\release_smoke.ipl', output / 'scratch layer.ipl')
            call('session', 'begin', 'release-smoke')
            placed = call('place', 1281, 2490, -1665, 14, 0)
            assert placed['instance_id'] >= 0
            rollback = call('session', 'rollback')
            assert rollback['live_instances'] == 0
            report['checks'].append('scratch placement and rollback')
            call('session', 'begin', 'release-save')
            call('place', 1281, 2490, -1665, 14, 0)
            call('session', 'commit')
            call('save')
            assert (output / 'scratch layer.ipl').exists()
            report['checks'].append('scratch commit and save outside game files')
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(timeout=5)
    report['checks'].append('process shutdown')
    report['ok'] = True
    (output / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'ok': True, 'checks': report['checks']}, indent=2))

if __name__ == '__main__':
    main()
