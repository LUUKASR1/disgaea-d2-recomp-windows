"""Prepare the official Windows runtimes required by the preparation helper."""
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

RUNTIMES = {
    'vc': ('https://download.visualstudio.microsoft.com/download/pr/ebdab8e5-1d7b-4d9f-a11b-cbb1720c3b12/843068991DAAA1F73AD9F6239BCE4D0F6A07A51F18C37EA2A867E9BECA71295C/VC_redist.x64.exe',
           '843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c', ['/install', '/passive', '/norestart']),
    'vulkan': ('https://vulkan.lunarg.com/sdk/download/1.4.363.0/windows/VulkanRT-X64-1.4.363.0-Installer.exe',
               '3f03bda2522d1d10d05461ce8a47364f9af6f5dc8642b7c45e43943dba164303', ['/S']),
}
VC_FILES = ('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')


def system_directory():
    buffer = ctypes.create_unicode_buffer(32768)
    if not ctypes.windll.kernel32.GetSystemDirectoryW(buffer, len(buffer)):
        raise ctypes.WinError()
    return Path(buffer.value)


def file_version(path):
    version = ctypes.WinDLL(str(system_directory() / 'version.dll'))
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    handle = wintypes.DWORD()
    size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(handle))
    if not size:
        return ()
    buffer = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
        return ()
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    if not version.VerQueryValueW(buffer, '\\', ctypes.byref(pointer), ctypes.byref(length)):
        return ()
    data = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    return (data[2] >> 16, data[2] & 65535, data[3] >> 16, data[3] & 65535)


def missing_runtimes():
    root = system_directory()
    missing = []
    if any(not (root / name).is_file() for name in VC_FILES) or file_version(root / 'msvcp140.dll') < (14, 44, 35211, 0):
        missing.append('vc')
    if not (root / 'vulkan-1.dll').is_file():
        missing.append('vulkan')
    return missing


def checksum(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def install_manifest(manifest, run=subprocess.run):
    """Verify every executable against built-in pins before running any installer."""
    items = json.loads(Path(manifest).read_text(encoding='utf-8'))
    if not isinstance(items, list) or not items or len(items) > len(RUNTIMES):
        raise ValueError('Lista de runtimes inválida.')
    validated = []
    seen = set()
    for item in items:
        if not isinstance(item, dict) or item.get('name') not in RUNTIMES or item['name'] in seen:
            raise ValueError('Runtime desconhecido ou repetido.')
        seen.add(item['name'])
        path = Path(item['path']).resolve()
        if not path.is_file() or checksum(path) != RUNTIMES[item['name']][1]:
            raise RuntimeError('Instalador de runtime falhou na verificação SHA-256.')
        validated.append((item['name'], path))
    results = []
    for name, path in validated:
        process = run([str(path), *RUNTIMES[name][2]], check=False, timeout=600)
        accepted = (0, 1638, 3010, 1641, 0x80070666) if name == 'vc' else (0,)
        if process.returncode not in accepted:
            raise RuntimeError('Instalador oficial ' + name + ' falhou: ' + str(process.returncode))
        results.append({'name': name, 'returncode': process.returncode})
    (Path(manifest).parent / 'runtime-install-result.json').write_text(json.dumps(results), encoding='utf-8')


def elevate_install(manifest):
    if ctypes.windll.shell32.IsUserAnAdmin():
        install_manifest(manifest)
        return
    class ExecuteInfo(ctypes.Structure):
        _fields_ = [('cbSize', wintypes.DWORD), ('fMask', wintypes.ULONG), ('hwnd', wintypes.HWND),
                    ('lpVerb', wintypes.LPCWSTR), ('lpFile', wintypes.LPCWSTR), ('lpParameters', wintypes.LPCWSTR),
                    ('lpDirectory', wintypes.LPCWSTR), ('nShow', ctypes.c_int), ('hInstApp', wintypes.HINSTANCE),
                    ('lpIDList', ctypes.c_void_p), ('lpClass', wintypes.LPCWSTR), ('hkeyClass', wintypes.HKEY),
                    ('dwHotKey', wintypes.DWORD), ('hIcon', wintypes.HANDLE), ('hProcess', wintypes.HANDLE)]
    arguments = ['--install-runtimes', str(Path(manifest).resolve())]
    if not getattr(sys, 'frozen', False):
        arguments.insert(0, str(Path(__file__).with_name('launcher.py')))
    info = ExecuteInfo()
    info.cbSize = ctypes.sizeof(info); info.fMask = 0x40
    info.lpVerb = 'runas'; info.lpFile = sys.executable
    info.lpParameters = subprocess.list2cmdline(arguments); info.nShow = 1
    shell = ctypes.windll.shell32
    shell.ShellExecuteExW.argtypes = [ctypes.POINTER(ExecuteInfo)]
    if not shell.ShellExecuteExW(ctypes.byref(info)):
        raise RuntimeError('O Windows não autorizou a instalação dos runtimes necessários.')
    kernel = ctypes.windll.kernel32
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    try:
        start = time.monotonic()
        while kernel.WaitForSingleObject(info.hProcess, 500) == 258:
            if time.monotonic() - start > 1200:
                raise RuntimeError('A instalação dos runtimes ainda não terminou. Aguarde o instalador do Windows.')
        code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(info.hProcess, ctypes.byref(code)) or code.value:
            raise RuntimeError('A instalação dos runtimes não foi concluída. Código: ' + str(code.value))
    finally:
        kernel.CloseHandle(info.hProcess)


def ensure_runtimes(home, download, notify, force=False):
    missing = list(RUNTIMES) if force else missing_runtimes()
    if not missing:
        return
    directory = Path(home) / 'runtime-setup'
    directory.mkdir(parents=True, exist_ok=True)
    items = []
    for name in missing:
        url, sha, _ = RUNTIMES[name]
        path = directory / (name + '-installer.exe')
        if not path.is_file() or checksum(path) != sha:
            notify('Baixando runtime oficial necessário: ' + name)
            download(url, path, sha)
        items.append({'name': name, 'path': str(path)})
    manifest = directory / 'install.json'
    manifest.write_text(json.dumps(items, ensure_ascii=False), encoding='utf-8')
    notify('Instalando bibliotecas de execução. O Windows pode pedir autorização; não são ferramentas de desenvolvimento.')
    elevate_install(manifest)
    if missing_runtimes():
        raise RuntimeError('Os runtimes ainda não estão disponíveis. Confira o instalador oficial; pode ser necessário reiniciar o Windows.')
