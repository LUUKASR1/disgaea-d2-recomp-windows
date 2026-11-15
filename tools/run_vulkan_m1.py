#!/usr/bin/env python3
"""Run the locally built Vulkan-enabled game with the user's existing launcher profile.

Experimental M1: Vulkan only presents the guest clear colour (no game drawing yet).
Saves/hdd0 are the same ones normal play uses. Opt-in env vars are set here, never globally.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime
import uuid

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / 'windows'))
from iniciar_build import launch_environment  # noqa: E402


def main():
    home = Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'DisgaeaD2Recomp-Graphics-Preview'
    config = json.loads((home / 'build-atual.json').read_text(encoding='utf-8'))
    exe = repo / 'port/build-windows-140/DisgaeaD2Recomp.exe'
    workspace = Path(config['workspace'])
    elf = workspace / 'work/v140/EBOOT.elf'
    iso = Path(config['iso_path'])
    for path in (exe, elf, iso):
        if not path.is_file():
            print('ERRO: arquivo ausente: ' + str(path), file=sys.stderr)
            return 2
    profile = json.loads((repo / 'windows/perfil.json').read_text(encoding='utf-8'))
    env = launch_environment(profile, config, os.environ)
    env.pop('D2_GAME_ISO', None)
    env['PS3_VFS_ROOT'] = str(config['game_root'])
    env['D2_GRAPHICS_API'] = 'vulkan'
    env['D2_VULKAN_EXPERIMENTAL'] = '1'
    # SPIR-V cache: avoids recompiling shaders with DXC on every run (compile hitches in gameplay).
    env['D2_VULKAN_CACHE_DIR'] = str(repo / 'vulkan-shader-cache')
    # Shader/state dump for the translation work. Contains data derived from the user's game: keep local.
    # Opt-in (set D2_VULKAN_DUMP=1): the dump and per-draw statistics cost frame time.
    if os.environ.get('D2_VULKAN_DUMP') == '1':
        env['D2_VULKAN_DUMP_DIR'] = str(repo / 'vulkan-dump')
        env['D2_VULKAN_STATS'] = '1'
    if len(sys.argv) > 1 and sys.argv[1] == '--d3d12':
        env['D2_GRAPHICS_API'] = 'd3d12'
        env.pop('D2_VULKAN_EXPERIMENTAL', None)
    logs = home / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / ('jogo-vulkan-m1-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6] + '.log')
    print('API: ' + env['D2_GRAPHICS_API'] + '\nLog: ' + str(log))
    with log.open('xb') as output:
        result = subprocess.run([str(exe), str(elf), '--iso', str(iso)], cwd=workspace, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
    print('Jogo encerrou com codigo ' + str(result.returncode))
    # Copy the log next to the project so it can be inspected easily.
    (repo / 'ultimo-log-vulkan.txt').write_bytes(log.read_bytes())
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
