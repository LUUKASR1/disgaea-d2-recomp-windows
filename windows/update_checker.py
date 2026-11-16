"""Read-only stable release checks. Never download or execute an installer."""
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.request

API_URL = 'https://api.github.com/repos/LUUKASR1/disgaea-d2-recomp-windows/releases/latest'
RELEASE_ROOT = 'https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/tag/'
MAX_RESPONSE = 262144


def version_tuple(value):
    if not isinstance(value, str) or not re.fullmatch(r'v?\d{1,6}\.\d{1,6}\.\d{1,6}', value):
        raise ValueError('Invalid stable version')
    return tuple(map(int, value.removeprefix('v').split('.')))


def parse_release(data, current):
    installed = version_tuple(current)
    if not isinstance(data, dict) or data.get('draft') is not False or data.get('prerelease') is not False:
        raise ValueError('Not a public stable release')
    tag = data.get('tag_name')
    remote = version_tuple(tag)
    # Ignore upstream-supplied URLs; only open the expected project's tag page.
    return {'available': remote > installed, 'version': '.'.join(map(str, remote)),
            'url': RELEASE_ROOT + tag}


def check_latest(current):
    version_tuple(current)
    request = urllib.request.Request(API_URL, headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'DisgaeaD2Launcher/' + current})
    with urllib.request.urlopen(request, timeout=8) as response:
        raw = response.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError('Release response too large')
    return parse_release(json.loads(raw.decode('utf-8')), current)


def auto_check_enabled(home):
    try:
        data = json.loads((Path(home) / 'launcher-updates.json').read_text(encoding='utf-8'))
        value = data.get('check_on_start') if isinstance(data, dict) else None
        return value if type(value) is bool else True
    except (OSError, ValueError):
        return True


def save_auto_check(home, enabled):
    if type(enabled) is not bool:
        raise ValueError('Invalid update preference')
    home = Path(home); home.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=home,
                                         prefix='launcher-updates-', suffix='.tmp', delete=False) as f:
            temporary = Path(f.name)
            json.dump({'check_on_start': enabled}, f)
        os.replace(temporary, home / 'launcher-updates.json')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
