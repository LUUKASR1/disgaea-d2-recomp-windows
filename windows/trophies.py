"""Trophy progress reset for the launcher. Files are moved to a backup folder, never deleted."""
from pathlib import Path
import shutil


def trophy_files(home):
    """Trophy progress files of every prepared build (the game writes gamedata/trophies/<commId>.json in its workspace)."""
    return sorted(p for p in (Path(home) / 'builds').glob('*/gamedata/trophies/*.json') if p.is_file())


def reset_trophies(home, stamp):
    """Moves every trophy file into trophy-backups/<stamp>/<build>/ and returns (moved count, backup folder).
    Call only while the game is closed: a running game keeps its trophies in memory and would write them back."""
    home = Path(home)
    files = trophy_files(home)
    backup = home / 'trophy-backups' / stamp
    for source in files:
        target = backup / source.parents[2].name / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(source), str(target))
    return len(files), backup
