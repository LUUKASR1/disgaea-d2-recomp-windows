"""The player's own title artwork (PS3_GAME/PIC1.PNG) for the launcher's home slides. Nothing from the game is shipped:
the picture is read at run time from the game folder or the ISO the player selected, and cached in the launcher folder."""
from pathlib import Path
import struct

from companion import _iso_find

PNG = b'\x89PNG\r\n\x1a\n'


def png_size(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(24)
    except OSError:
        return None
    if len(head) < 24 or head[:8] != PNG:
        return None
    return struct.unpack('>II', head[16:24])


def _extract_from_iso(iso_path, name, out_path):
    try:
        with open(iso_path, 'rb') as f:
            f.seek(16 * 2048)
            pvd = f.read(2048)
            if len(pvd) < 2048 or pvd[0] != 1 or pvd[1:6] != b'CD001':
                return False
            root = (struct.unpack('<I', pvd[158:162])[0], struct.unpack('<I', pvd[166:170])[0])
            folder = _iso_find(f, root, 'PS3_GAME', True)
            entry = _iso_find(f, folder, name, False) if folder else None
            if entry is None or not 0 < entry[1] <= 16 * 1024 * 1024:
                return False
            f.seek(entry[0] * 2048)
            data = f.read(entry[1])
        if data[:8] != PNG:
            return False
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Path(out_path).write_bytes(data)
        return True
    except (OSError, struct.error):
        return False


def find_title_art(home, game_root=None, iso_path=None):
    """Path to a usable PIC1.PNG (at least 760x430) or None."""
    candidates = []
    if game_root:
        candidates.append(Path(game_root) / 'PS3_GAME/PIC1.PNG')
    cached = Path(home) / 'player-art/PIC1.PNG'
    if iso_path and not cached.is_file() and Path(iso_path).is_file():
        _extract_from_iso(iso_path, 'PIC1.PNG', cached)
    candidates.append(cached)
    for path in candidates:
        size = png_size(path)
        if size and size[0] >= 760 and size[1] >= 430:
            return path
    return None
