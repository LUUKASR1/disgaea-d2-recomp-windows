import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import display_settings as display
import launcher


class DisplayTests(unittest.TestCase):
    def test_refresh_settings_migrate_and_override_environment(self):
        migrated=display.validate_settings({})
        self.assertFalse(migrated['maximum_refresh']); self.assertFalse(migrated['vrr'])
        env=display.display_environment({'D2_MAX_REFRESH':'1','D2_VRR':'1'},migrated)
        self.assertEqual((env['D2_MAX_REFRESH'],env['D2_VRR']),('0','0'))
        enabled=dict(migrated,maximum_refresh=True,vrr=True)
        env=display.display_environment({},enabled)
        self.assertEqual((env['D2_MAX_REFRESH'],env['D2_VRR']),('1','1'))
        for name in ('maximum_refresh','vrr'):
            for invalid in (1,'true',None):
                with self.assertRaises(ValueError):display.validate_settings(dict(migrated,**{name:invalid}))

    def test_hud_output_migrates_off_and_overrides_inherited_flag(self):
        self.assertFalse(display.validate_settings({'resolution':'1920x1080'})['hud_after_upscale'])
        self.assertEqual(display.display_environment({'D2_HUD_AFTER_UPSCALE':'1'},display.DEFAULTS)['D2_HUD_AFTER_UPSCALE'],'0')
        enabled=dict(display.DEFAULTS,hud_after_upscale=True)
        self.assertEqual(display.display_environment({},enabled)['D2_HUD_AFTER_UPSCALE'],'1')
        for value in (1,'true',None):
            with self.assertRaises(ValueError):display.validate_settings(dict(enabled,hud_after_upscale=value))
    def test_defaults_keep_existing_profile(self):
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(display.load_settings(home), display.DEFAULTS)
        env = {'D2_PRESENT_INTERVAL': '8', 'PS3_VFS_ROOT': 'owned', 'OTHER': 'keep'}
        result = display.display_environment(env, display.DEFAULTS)
        self.assertEqual(result['D2_PRESENT_INTERVAL'], '0')
        self.assertEqual(result['PS3_VFS_ROOT'], 'owned')
        self.assertEqual(result['OTHER'], 'keep')
        self.assertEqual(env['D2_PRESENT_INTERVAL'], '8')

    def test_settings_roundtrip_preserves_build_and_save(self):
        with tempfile.TemporaryDirectory() as root:
            home = Path(root)
            (home / 'build-atual.json').write_bytes(b'working build')
            (home / 'save.dat').write_bytes(b'owned save')
            value = {'resolution': '2560x1440', 'mode': 'borderless', 'vsync': True}
            display.save_settings(home, value)
            self.assertEqual(display.load_settings(home), dict(display.DEFAULTS, **value))
            self.assertEqual((home / 'build-atual.json').read_bytes(), b'working build')
            self.assertEqual((home / 'save.dat').read_bytes(), b'owned save')
            self.assertFalse((home / 'launcher-settings.tmp').exists())
            self.assertEqual(display.display_environment({}, value)['D2_PRESENT_INTERVAL'], '1')

    def test_reject_invalid_settings_and_preserve_previous(self):
        with tempfile.TemporaryDirectory() as root:
            home = Path(root)
            display.save_settings(home, display.DEFAULTS)
            original = (home / 'launcher-settings.json').read_bytes()
            for invalid in ([], {'resolution': '99999x2'}, {'mode': 'exclusive'}, {'vsync': 'false'}, {'vsync': 1}):
                with self.assertRaises(ValueError):
                    display.save_settings(home, invalid)
                self.assertEqual((home / 'launcher-settings.json').read_bytes(), original)

    def test_corrupt_settings_requires_explicit_reset(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'launcher-settings.json'; path.write_text('{broken')
            with self.assertRaisesRegex(ValueError, 'Restaurar padrão'):
                display.load_settings(root)
            self.assertEqual(path.read_text(), '{broken')
            display.save_settings(root, display.DEFAULTS)
            self.assertEqual(display.load_settings(root), display.DEFAULTS)

    def test_render_size_and_output_size_are_independent(self):
        value = dict(display.DEFAULTS, resolution='2560x1440', render_resolution='1280x720', upscaler='fsr1')
        env = display.display_environment({'D2_UPSCALER': 'dlss'}, value)
        self.assertEqual((env['D2_RENDER_WIDTH'], env['D2_OUTPUT_WIDTH']), ('1280', '2560'))
        self.assertEqual(env['D2_UPSCALER'], 'fsr1')
        for bad in ({'upscaler': 'dlss'}, {'frame_generation': True}, {'fsr_sharpness': float('nan')},
                    {'fsr_sharpness': True}, {'resolution': '1280x720', 'render_resolution': '3840x2160', 'upscaler': 'fsr1'}):
            with self.assertRaises(ValueError): display.validate_settings(dict(display.DEFAULTS, **bad))

    def test_window_fits_monitor_and_retains_aspect(self):
        value = dict(display.DEFAULTS, resolution='3840x2160')
        x, y, width, height = display.window_bounds(value, (0, 0, 1920, 1040), (0, 0, 1920, 1080), (16, 39))
        self.assertGreaterEqual(x, 0); self.assertGreaterEqual(y, 0)
        self.assertLessEqual(x + width, 1920); self.assertLessEqual(y + height, 1040)
        self.assertAlmostEqual((width - 16) / (height - 39), 16 / 9, places=2)
        self.assertEqual(display.window_bounds(display.DEFAULTS, (0, 0, 1920, 1040), (0, 0, 1920, 1080), (16, 39)), (312, 140, 1296, 759))

    def test_borderless_uses_monitor_with_negative_origin(self):
        value = dict(display.DEFAULTS, mode='borderless')
        self.assertEqual(display.window_bounds(value, (-1920, 0, 0, 1040), (-1920, 0, 0, 1080), (16, 39)), (-1920, 0, 1920, 1080))

    def test_process_start_callback_and_exit_log(self):
        with tempfile.TemporaryDirectory() as root:
            backend = launcher.Backend(lambda message: None, home=root)
            observed = []
            backend.run([sys.executable, '-c', 'print("owned fixture")'], on_started=lambda process: observed.append(process.pid))
            self.assertEqual(len(observed), 1)
            self.assertIn('codigo_saida=0', (Path(root) / 'preparacao.log').read_text())


@unittest.skipUnless(os.name == 'nt', 'Win32 integration test')
class WindowsDisplayTests(unittest.TestCase):
    def test_real_game_class_window_resize_and_borderless(self):
        from ctypes import wintypes as w
        user = ctypes.WinDLL('user32', use_last_error=True)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetModuleHandleW.argtypes = [w.LPCWSTR]; kernel.GetModuleHandleW.restype = w.HMODULE
        proc_type = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
        user.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]; user.DefWindowProcW.restype = ctypes.c_ssize_t
        @proc_type
        def wndproc(hwnd, msg, wp, lp):
            return user.DefWindowProcW(hwnd, msg, wp, lp)
        class WindowClass(ctypes.Structure):
            _fields_ = [('style', w.UINT), ('lpfnWndProc', proc_type), ('cbClsExtra', ctypes.c_int), ('cbWndExtra', ctypes.c_int), ('hInstance', w.HINSTANCE), ('hIcon', w.HICON), ('hCursor', w.HANDLE), ('hbrBackground', w.HBRUSH), ('lpszMenuName', w.LPCWSTR), ('lpszClassName', w.LPCWSTR)]
        user.RegisterClassW.argtypes = [ctypes.POINTER(WindowClass)]; user.RegisterClassW.restype = w.ATOM
        user.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]; user.CreateWindowExW.restype = w.HWND
        user.DestroyWindow.argtypes = [w.HWND]; user.DestroyWindow.restype = w.BOOL
        user.UnregisterClassW.argtypes = [w.LPCWSTR, w.HINSTANCE]; user.UnregisterClassW.restype = w.BOOL
        user.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]; user.GetClientRect.restype = w.BOOL
        user.GetWindowLongW.argtypes = [w.HWND, ctypes.c_int]; user.GetWindowLongW.restype = w.LONG
        instance = kernel.GetModuleHandleW(None)
        wc = WindowClass(); wc.lpfnWndProc = wndproc; wc.hInstance = instance; wc.lpszClassName = 'ps3recomp_d3d12'
        self.assertTrue(user.RegisterClassW(ctypes.byref(wc)))
        hwnd = user.CreateWindowExW(0, wc.lpszClassName, 'Owned display fixture', 0x00CF0000, 0, 0, 320, 240, None, None, instance, None)
        self.assertTrue(hwnd)
        try:
            class Process:
                pid = os.getpid()
                def poll(self): return None
            notifications = []
            result = display.configure_game_window(Process(), display.DEFAULTS, notifications.append, threading.Event(), timeout=1)
            self.assertIsNotNone(result)
            rect = w.RECT(); self.assertTrue(user.GetClientRect(hwnd, ctypes.byref(rect)))
            # CI monitor may be smaller than 720p; clamping must preserve 16:9.
            self.assertAlmostEqual(rect.right / rect.bottom, 16 / 9, places=2)
            self.assertGreater(rect.right, 320)
            display.apply_window(hwnd, dict(display.DEFAULTS, mode='borderless'))
            self.assertEqual(user.GetWindowLongW(hwnd, -16) & 0x00CF0000, 0)
            self.assertEqual(len(notifications), 1)
        finally:
            user.DestroyWindow(hwnd); user.UnregisterClassW(wc.lpszClassName, instance)

    def test_gui_pages_and_settings_without_game(self):
        from launcher_gui import run_gui
        with tempfile.TemporaryDirectory() as root:
            app, show = run_gui(launcher.Backend, Path(root), 'test', lambda directory: False, preview=True)
            try:
                for page in ('Início', 'Vídeo', 'Preparação', 'Arquivos e suporte'):
                    show(page); app.update()
                self.assertTrue(app.winfo_exists())
                self.assertFalse((Path(root) / 'launcher-settings.json').exists())
                self.assertFalse((Path(root) / 'build-atual.json').exists())
            finally:
                app.destroy()


if __name__ == '__main__':
    unittest.main()
