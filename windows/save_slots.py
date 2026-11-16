"""Save backups ("slots") for the PS3 savedata folder.

Backups are plain copies of the savedata directory kept next to the game data
(<home>/save-backups/<timestamp>[-<label>]). Restoring never discards the
current saves: they are copied into a new backup first. Nothing is converted
or decrypted, and nothing runs while the game is open (callers must check).
"""
import re
import shutil
import time
from pathlib import Path

from save_access import save_directory

AUTO_LABEL = 'auto'
KEEP_AUTO = 5


def backups_directory(home):
    return Path(home) / 'save-backups'


def _safe_label(label):
    label = re.sub(r'[^0-9A-Za-zÀ-ÿ _-]+', '', label or '').strip().replace(' ', '-')
    return label[:40]


def _unique(base):
    candidate, n = base, 1
    while candidate.exists():
        n += 1
        candidate = base.with_name(f'{base.name}-{n}')
    return candidate


def _copy_tree(source, target):
    # Fully copied into a temporary name, then renamed: a half-copied backup never looks valid.
    tmp = target.with_name(target.name + '.tmp')
    if tmp.exists():
        shutil.rmtree(tmp)
    shutil.copytree(source, tmp)
    tmp.rename(target)


def create_backup(home, label=''):
    """Copy the current savedata into a new backup folder; returns it, or None if there is nothing to copy."""
    source = save_directory(home)
    if not source.is_dir() or not any(source.iterdir()):
        return None
    root = backups_directory(home)
    root.mkdir(parents=True, exist_ok=True)
    name = time.strftime('%Y-%m-%d_%H-%M-%S')
    label = _safe_label(label)
    target = _unique(root / (f'{name}-{label}' if label else name))
    _copy_tree(source, target)
    return target


def list_backups(home):
    """Backups, newest first, as (path, display name, number of save folders)."""
    root = backups_directory(home)
    if not root.is_dir():
        return []
    items = [p for p in root.iterdir() if p.is_dir() and not p.name.endswith('.tmp')]
    items.sort(key=lambda p: p.name, reverse=True)
    return [(p, p.name, sum(1 for c in p.iterdir() if c.is_dir())) for p in items]


def prune_auto_backups(home, keep=KEEP_AUTO):
    autos = [p for p, name, _ in list_backups(home) if f'-{AUTO_LABEL}' in name]
    for old in autos[keep:]:
        shutil.rmtree(old, ignore_errors=True)


def auto_backup(home):
    """Backup taken before each game launch; only the newest KEEP_AUTO are kept."""
    target = create_backup(home, AUTO_LABEL)
    if target:
        prune_auto_backups(home)
    return target


def restore_backup(home, backup):
    """Replace the current savedata with a backup. The current saves are backed up first ('antes-de-restaurar')."""
    backup = Path(backup)
    root = backups_directory(home).resolve()
    if backup.resolve().parent != root or not backup.is_dir():
        raise ValueError('backup inválido')
    safety = create_backup(home, 'antes-de-restaurar')
    target = save_directory(home)
    staging = target.with_name(target.name + '.restaurando')
    if staging.exists():
        shutil.rmtree(staging)
    shutil.copytree(backup, staging)
    if target.exists():
        shutil.rmtree(target)
    staging.rename(target)
    return safety


def delete_backup(home, backup):
    backup = Path(backup)
    if backup.resolve().parent != backups_directory(home).resolve() or not backup.is_dir():
        raise ValueError('backup inválido')
    shutil.rmtree(backup)
