"""Locate the same HDD root used by the game; never copy or replace saves."""
import json
import os
from pathlib import Path

SAVE_HELP = '''1. Feche o jogo antes de copiar qualquer save.
2. Faça uma cópia de segurança da pasta atual.
3. Copie a pasta completa do seu save compatível para a pasta abaixo, preservando seu nome e os arquivos internos. Não coloque uma pasta savedata dentro de outra.
4. Abra o jogo e use Carregar.

Destino:
{path}

Saves do PS3 precisam estar em formato compatível e descriptografado; copiar um backup cifrado do console não é suficiente. A compatibilidade entre regiões/versões não é garantida. O launcher não converte, descriptografa nem sobrescreve saves automaticamente.'''


def save_directory(home):
    home = Path(home)
    standalone = home.name in ('DisgaeaD2Recomp-Standalone', 'DisgaeaD2Recomp-Graphics-Preview')
    hdd = home / ('hdd0' if standalone else 'auxiliar-rpcs3/dev_hdd0')
    try:
        data = json.loads((home / 'build-atual.json').read_text(encoding='utf-8'))
        configured = data.get('hdd0_root') if isinstance(data, dict) else None
        if isinstance(configured, str) and configured and Path(configured).is_absolute():
            hdd = Path(configured)
    except (OSError, ValueError):
        pass
    return hdd / 'home/00000001/savedata'


def open_save_directory(home, opener=None):
    target = save_directory(home)
    target.mkdir(parents=True, exist_ok=True)
    (opener or os.startfile)(str(target))
    return target
