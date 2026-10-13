"""Launcher-owned presentation settings; no game or save mutations."""
import json
from pathlib import Path
import time
import math

RESOLUTIONS = ('1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160')
RENDER_RESOLUTIONS = ('native', '640x360', '960x540', *RESOLUTIONS)
DEFAULTS = {'resolution': '1280x720', 'mode': 'windowed', 'vsync': False,
            'render_resolution': 'native', 'upscaler': 'native', 'fsr_sharpness': 0.2, 'frame_generation': False,
            'hud_after_upscale': False}


def validate_settings(value):
    if not isinstance(value, dict):
        raise ValueError('Configurações de vídeo inválidas.')
    result = {name: value.get(name, default) for name, default in DEFAULTS.items()}
    if result['resolution'] not in RESOLUTIONS:
        raise ValueError('Selecione uma resolução da lista.')
    if result['mode'] not in ('windowed', 'borderless'):
        raise ValueError('Modo de exibição inválido.')
    if type(result['vsync']) is not bool:
        raise ValueError('VSync deve estar ligado ou desligado.')
    if result['render_resolution'] not in RENDER_RESOLUTIONS:
        raise ValueError('Resolução interna inválida.')
    if result['upscaler'] not in ('native', 'fsr1'):
        raise ValueError('DLSS/FSR temporal ainda indisponível nesta versão.')
    sharp = result['fsr_sharpness']
    if type(sharp) not in (int, float) or not math.isfinite(sharp) or not 0 <= sharp <= 1:
        raise ValueError('Nitidez FSR deve estar entre 0 e 1.')
    if result['frame_generation'] is not False:
        raise ValueError('Frame generation ainda indisponível nesta versão.')
    if type(result['hud_after_upscale']) is not bool:
        raise ValueError('HUD após upscaling deve estar ligada ou desligada.')
    rw, rh = map(int, ('1280x720' if result['render_resolution'] == 'native' else result['render_resolution']).split('x'))
    ow, oh = map(int, result['resolution'].split('x'))
    if result['upscaler'] == 'fsr1' and (rw > ow or rh > oh):
        raise ValueError('FSR 1 requer resolução interna menor ou igual à saída.')
    return result


def load_settings(home):
    path = Path(home) / 'launcher-settings.json'
    if not path.exists():
        return dict(DEFAULTS)
    try:
        return validate_settings(json.loads(path.read_text(encoding='utf-8')))
    except (ValueError, TypeError) as error:
        raise ValueError('Não foi possível ler launcher-settings.json. Use Restaurar padrão na aba Vídeo.') from error


def save_settings(home, value):
    result = validate_settings(value)
    home = Path(home)
    home.mkdir(parents=True, exist_ok=True)
    target = home / 'launcher-settings.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    temporary.replace(target)
    return result


def display_environment(inherited, settings):
    env = inherited.copy()
    value = validate_settings(settings)
    env['D2_PRESENT_INTERVAL'] = '1' if value['vsync'] else '0'
    rw, rh = ('1280x720' if value['render_resolution'] == 'native' else value['render_resolution']).split('x')
    ow, oh = value['resolution'].split('x')
    env.update(D2_RENDER_WIDTH=rw, D2_RENDER_HEIGHT=rh, D2_OUTPUT_WIDTH=ow, D2_OUTPUT_HEIGHT=oh,
               D2_UPSCALER=value['upscaler'], D2_FSR_SHARPNESS=str(value['fsr_sharpness']),
               D2_HUD_AFTER_UPSCALE='1' if value['hud_after_upscale'] else '0')
    return env


def window_bounds(settings, work, monitor, frame):
    """Pure physical-pixel geometry, kept inside the selected monitor."""
    settings = validate_settings(settings)
    if settings['mode'] == 'borderless':
        left, top, right, bottom = monitor
        return left, top, right - left, bottom - top
    left, top, right, bottom = work
    border_w, border_h = frame
    available_w, available_h = max(1, right - left - border_w), max(1, bottom - top - border_h)
    width, height = map(int, settings['resolution'].split('x'))
    scale = min(1.0, available_w / width, available_h / height)
    width, height = max(1, int(width * scale)), max(1, int(height * scale))
    outer_w, outer_h = width + border_w, height + border_h
    return left + (right - left - outer_w) // 2, top + (bottom - top - outer_h) // 2, outer_w, outer_h


def apply_window(hwnd, settings):
    """Resize only the HWND previously matched to the launched process."""
    import ctypes as c
    from ctypes import wintypes as w
    user = c.WinDLL('user32', use_last_error=True)
    class MonitorInfo(c.Structure):
        _fields_ = [('cbSize', w.DWORD), ('rcMonitor', w.RECT), ('rcWork', w.RECT), ('dwFlags', w.DWORD)]
    user.MonitorFromWindow.argtypes = [w.HWND, w.DWORD]
    user.MonitorFromWindow.restype = w.HANDLE
    user.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(MonitorInfo)]
    user.GetMonitorInfoW.restype = w.BOOL
    user.GetWindowLongW.argtypes = [w.HWND, c.c_int]
    user.GetWindowLongW.restype = w.LONG
    user.SetWindowLongW.argtypes = [w.HWND, c.c_int, w.LONG]
    user.SetWindowLongW.restype = w.LONG
    user.AdjustWindowRectEx.argtypes = [c.POINTER(w.RECT), w.DWORD, w.BOOL, w.DWORD]
    user.AdjustWindowRectEx.restype = w.BOOL
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    user.SetWindowPos.restype = w.BOOL
    settings = validate_settings(settings)
    # Match the game's DPI context for monitor coordinates and client sizing.
    user.GetWindowDpiAwarenessContext.argtypes = [w.HWND]
    user.GetWindowDpiAwarenessContext.restype = w.HANDLE
    user.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
    user.SetThreadDpiAwarenessContext.restype = w.HANDLE
    previous = user.SetThreadDpiAwarenessContext(user.GetWindowDpiAwarenessContext(hwnd))
    try:
        info = MonitorInfo(); info.cbSize = c.sizeof(info)
        if not user.GetMonitorInfoW(user.MonitorFromWindow(hwnd, 2), c.byref(info)):
            raise c.WinError(c.get_last_error())
        style = user.GetWindowLongW(hwnd, -16)
        exstyle = user.GetWindowLongW(hwnd, -20)
        if settings['mode'] == 'borderless':
            c.set_last_error(0)
            if not user.SetWindowLongW(hwnd, -16, (style & ~0x00CF0000) | 0x80000000) and c.get_last_error():
                raise c.WinError(c.get_last_error())
            frame = (0, 0)
        else:
            rect = w.RECT(0, 0, 1280, 720)
            if not user.AdjustWindowRectEx(c.byref(rect), style, False, exstyle):
                raise c.WinError(c.get_last_error())
            frame = (rect.right - rect.left - 1280, rect.bottom - rect.top - 720)
        coords = lambda rect: (rect.left, rect.top, rect.right, rect.bottom)
        x, y, width, height = window_bounds(settings, coords(info.rcWork), coords(info.rcMonitor), frame)
        if not user.SetWindowPos(hwnd, None, x, y, width, height, 0x0020 | 0x0004 | 0x0010):
            raise c.WinError(c.get_last_error())
        return width, height
    finally:
        if previous:
            user.SetThreadDpiAwarenessContext(previous)


def configure_game_window(process, settings, notify, stop, timeout=30):
    """Wait for the game's D3D12 window, without blocking launcher or game."""
    import ctypes as c
    from ctypes import wintypes as w
    user = c.WinDLL('user32', use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.EnumWindows.restype = w.BOOL
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype = w.DWORD
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetClassNameW.restype = c.c_int
    deadline = time.monotonic() + timeout
    while not stop.is_set() and process.poll() is None and time.monotonic() < deadline:
        found = []
        @callback_type
        def visit(hwnd, unused):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == process.pid:
                name = c.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if name.value == 'ps3recomp_d3d12':
                    found.append(hwnd)
            return True
        if not user.EnumWindows(visit, 0):
            raise c.WinError(c.get_last_error())
        if found:
            width, height = apply_window(found[0], settings)
            notify('Exibição aplicada. A imagem usa a resolução interna original do jogo.')
            return width, height
        stop.wait(0.1)
    if not stop.is_set() and process.poll() is None:
        notify('A janela não foi encontrada a tempo. O jogo continua com a exibição padrão.')
    return None
