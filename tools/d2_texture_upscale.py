#!/usr/bin/env python3
"""Upscale the game's dumped textures into a texture pack (offline).

1. Play with "Salvar texturas" on (Rodar-Jogo-Dump-Texturas.bat) so that
   %LOCALAPPDATA%\\DisgaeaD2Recomp\\texture-pack\\dump\\ fills up with PNGs.
2. Run this tool. It writes bigger versions, with the same file names, to
   %LOCALAPPDATA%\\DisgaeaD2Recomp\\texture-pack\\ -- the game picks them up on the next start.

Engines
  lanczos      Pillow only (pip install pillow). Premultiplied-alpha Lanczos + light sharpening. Fast, no download.
  realesrgan   AI upscaler. Needs the realesrgan-ncnn-vulkan executable (github.com/xinntao/Real-ESRGAN-ncnn-vulkan,
               release zip with models). Pass it with --realesrgan C:\\path\\realesrgan-ncnn-vulkan.exe.
  auto         realesrgan when --realesrgan (or D2_REALESRGAN) is set, otherwise lanczos.

Palette textures (8-bit index images, 256x1 palettes, two-channel fonts) are skipped: they only make sense with
their palette, so a replacement would show wrong colours.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NAME = re.compile(r'^([0-9a-f]{16})_(\d+)x(\d+)_f([0-9A-F]{2})\.png$')
# Format bytes (& 0x9F) whose decoded image is plain colour: A1R5G5B5/A4R4G4B4/R5G6B5/A8R8G8B8/DXT1/3/5/D8R8G8B8...
COLOUR_FORMATS = {0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x97, 0x9D, 0x9E}


def default_root() -> Path:
    base = os.environ.get('D2_TEXTURE_PACK_DIR')
    if base:
        return Path(base)
    local = os.environ.get('D2_SETTINGS_DIR') or os.path.join(os.environ.get('LOCALAPPDATA', str(Path.home())), 'DisgaeaD2Recomp')
    return Path(local) / 'texture-pack'


def candidates(src: Path, dst: Path, force: bool, min_side: int):
    for path in sorted(src.glob('*.png')):
        m = NAME.match(path.name)
        if not m:
            continue
        w, h, fmt = int(m.group(2)), int(m.group(3)), int(m.group(4), 16)
        if (fmt & 0x9F) not in COLOUR_FORMATS or w < min_side or h < min_side:
            continue
        if not force and (dst / path.name).exists():
            continue
        yield path


def lanczos(path: Path, out: Path, scale: int, sharpen: float):
    from PIL import Image, ImageFilter
    img = Image.open(path).convert('RGBA')
    size = (img.width * scale, img.height * scale)
    big = img.convert('RGBa').resize(size, Image.LANCZOS).convert('RGBA')  # premultiplied: no dark halos
    if sharpen > 0:
        rgb = big.convert('RGB').filter(ImageFilter.UnsharpMask(radius=1.2 * scale / 2, percent=int(60 * sharpen), threshold=2))
        big = Image.merge('RGBA', (*rgb.split(), big.getchannel('A')))
    big.save(out, optimize=True)


def realesrgan(files, dst: Path, exe: str, model: str, scale: int, extra_resize: bool):
    from PIL import Image
    with tempfile.TemporaryDirectory() as tmp:
        tin, tout = Path(tmp, 'in'), Path(tmp, 'out')
        tin.mkdir()
        tout.mkdir()
        for f in files:
            shutil.copy2(f, tin / f.name)
        cmd = [exe, '-i', str(tin), '-o', str(tout), '-n', model, '-s', '4', '-f', 'png']
        print('>', ' '.join(cmd), flush=True)
        subprocess.run(cmd, check=True, cwd=str(Path(exe).parent))
        for f in files:
            got = tout / f.name
            if not got.exists():
                print('  falhou:', f.name)
                continue
            if scale != 4:
                img = Image.open(got).convert('RGBA')
                src = Image.open(f)
                img = img.resize((src.width * scale, src.height * scale), Image.LANCZOS)
                img.save(dst / f.name, optimize=True)
            else:
                shutil.move(str(got), dst / f.name)


def main():
    root = default_root()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', type=Path, default=root / 'dump', help='pasta com os PNGs salvos pelo jogo')
    ap.add_argument('--dst', type=Path, default=root, help='pasta do pacote de texturas')
    ap.add_argument('--scale', type=int, default=2, choices=(2, 3, 4), help='fator de ampliacao (padrao 2)')
    ap.add_argument('--engine', choices=('auto', 'lanczos', 'realesrgan'), default='auto')
    ap.add_argument('--realesrgan', default=os.environ.get('D2_REALESRGAN'), help='caminho do realesrgan-ncnn-vulkan.exe')
    ap.add_argument('--model', default='realesrgan-x4plus-anime', help='modelo do Real-ESRGAN (pasta models)')
    ap.add_argument('--sharpen', type=float, default=1.0, help='nitidez do motor lanczos (0 desliga)')
    ap.add_argument('--min-side', type=int, default=16, help='ignora texturas menores que isso')
    ap.add_argument('--limit', type=int, default=0, help='processa so as N primeiras (teste)')
    ap.add_argument('--force', action='store_true', help='refaz mesmo as que ja existem')
    a = ap.parse_args()

    if not a.src.is_dir():
        print('Pasta de origem nao existe:', a.src, '\nJogue com o dump ligado primeiro (Rodar-Jogo-Dump-Texturas.bat).')
        return 2
    try:
        import PIL  # noqa: F401
    except ImportError:
        print('Falta o Pillow: execute  python -m pip install pillow')
        return 2
    engine = a.engine
    if engine == 'auto':
        engine = 'realesrgan' if a.realesrgan else 'lanczos'
    if engine == 'realesrgan' and not (a.realesrgan and Path(a.realesrgan).is_file()):
        print('Informe --realesrgan com o caminho do realesrgan-ncnn-vulkan.exe')
        return 2
    a.dst.mkdir(parents=True, exist_ok=True)
    files = list(candidates(a.src, a.dst, a.force, a.min_side))
    if a.limit:
        files = files[:a.limit]
    print(f'{len(files)} textura(s) para ampliar {a.scale}x com {engine} -> {a.dst}')
    if not files:
        return 0
    if engine == 'realesrgan':
        realesrgan(files, a.dst, a.realesrgan, a.model, a.scale, True)
    else:
        for i, f in enumerate(files, 1):
            lanczos(f, a.dst / f.name, a.scale, a.sharpen)
            if i % 20 == 0 or i == len(files):
                print(f'  {i}/{len(files)}', flush=True)
    print('Pronto. Abra o jogo: as texturas novas entram sozinhas.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
