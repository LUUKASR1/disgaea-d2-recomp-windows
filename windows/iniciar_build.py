#!/usr/bin/env python3
"""Inicia a instalacao atual com perfil normal, sem recompilar."""
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid


def launch_environment(profile, config, inherited):
    env = inherited.copy()
    for name in profile['clear']:
        env.pop(name, None)
    env.update(profile['environment'])
    env['PS3_VFS_ROOT'] = str(config['game_root'])
    env['PS3_HDD0_ROOT'] = str(config['hdd0_root'])
    return env


def main():
    if os.name != 'nt':
        raise RuntimeError('Este lancador e para Windows.')
    kit = Path(__file__).resolve().parent
    local = kit / 'config.local.json'
    config = json.loads((local if local.is_file() else kit / 'config.exemplo.json').read_text(encoding='utf-8'))
    repo = Path(__file__).resolve().parents[1]
    exe = repo / 'port/build-windows-140/DisgaeaD2Recomp.exe'
    elf = repo / 'work/v140/EBOOT.elf'
    for path in [exe, elf, Path(config['hdd0_root']) / 'game/BLUS31313/USRDIR/Data/START_7.dat']:
        if not path.is_file():
            raise RuntimeError('Arquivo necessario ausente: ' + str(path))
    if not (Path(config['game_root']) / 'PS3_GAME/USRDIR').is_dir():
        raise RuntimeError('Jogo base nao encontrado. Ajuste config.local.json.')
    profile = json.loads((kit / 'perfil.json').read_text())
    env = launch_environment(profile, config, os.environ)
    logs = Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'DisgaeaD2Recomp/logs'
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / ('jogo-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6] + '.log')
    print('Iniciando a build existente. Diagnosticos de desempenho desligados.')
    print('Log: ' + str(log))
    with log.open('xb') as output:
        result = subprocess.run([str(exe), str(elf)], cwd=repo,
                                env=env, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        print('O jogo encerrou com codigo ' + str(result.returncode))
    return result.returncode


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, KeyError, ValueError) as exc:
        print('ERRO: ' + str(exc), file=sys.stderr)
        sys.exit(1)
