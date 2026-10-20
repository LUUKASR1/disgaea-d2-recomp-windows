import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
import update_checker as updates


def release(tag='v0.2.4', **changes):
    return dict(tag_name=tag, draft=False, prerelease=False, **changes)


class UpdateCheckerTests(unittest.TestCase):
    def test_numeric_versions_and_only_newer_stable(self):
        self.assertTrue(updates.parse_release(release('v0.2.10'), '0.2.9')['available'])
        for tag in ('v0.2.3', 'v0.2.2'):
            self.assertFalse(updates.parse_release(release(tag), '0.2.3')['available'])
        for value in ('0.2.4-preview', 'v0.2', '../tag', '0.2.4/extra', None, '9999999.0.0'):
            with self.assertRaises(ValueError): updates.parse_release(release(value), '0.2.3')

    def test_drafts_prereleases_and_invalid_responses_rejected(self):
        for data in ([], {}, {'tag_name':'v0.2.4'}, dict(release(), draft=True),
                     dict(release(), prerelease=True), dict(release(), draft=0)):
            with self.assertRaises(ValueError): updates.parse_release(data, '0.2.3')

    def test_url_is_fixed_to_project_not_response(self):
        result = updates.parse_release(release(html_url='https://example.invalid/install.exe'), '0.2.3')
        self.assertEqual(result['url'], updates.RELEASE_ROOT + 'v0.2.4')

    def test_http_is_bounded_and_has_timeout(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size):
                self.size = size
                return json.dumps(release()).encode()
        response = Response()
        with patch('urllib.request.urlopen', return_value=response) as request:
            self.assertTrue(updates.check_latest('0.2.3')['available'])
        self.assertEqual(response.size, updates.MAX_RESPONSE + 1)
        self.assertEqual(request.call_args.kwargs['timeout'], 8)
        self.assertEqual(request.call_args.args[0].full_url, updates.API_URL)

    def test_network_errors_and_large_responses_propagate_without_files(self):
        with patch('urllib.request.urlopen', side_effect=OSError('offline')):
            with self.assertRaises(OSError): updates.check_latest('0.2.3')
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size): return b'x' * size
        with patch('urllib.request.urlopen', return_value=Response()):
            with self.assertRaises(ValueError): updates.check_latest('0.2.3')

    def test_preference_is_atomic_and_preserves_saves_and_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            settings = home / 'launcher-settings.json'; settings.write_bytes(b'video-settings')
            save = home / 'save.bin'; save.write_bytes(b'own-save')
            self.assertTrue(updates.auto_check_enabled(home))
            for enabled in (False, True):
                updates.save_auto_check(home, enabled)
                self.assertEqual(updates.auto_check_enabled(home), enabled)
            self.assertEqual(settings.read_bytes(), b'video-settings')
            self.assertEqual(save.read_bytes(), b'own-save')
            self.assertFalse(list(home.glob('*.tmp')))
            with self.assertRaises(ValueError): updates.save_auto_check(home, 'false')
            for text in ('[]', 'broken', '{"check_on_start":"false"}'):
                (home / 'launcher-updates.json').write_text(text)
                self.assertTrue(updates.auto_check_enabled(home))

    @unittest.skipUnless(os.name == 'nt', 'Actual Win32 launcher UI')
    def test_ui_notifies_translates_opens_safe_release_and_stays_playable_offline(self):
        from launcher import Backend
        from launcher_gui import run_gui
        with tempfile.TemporaryDirectory() as tmp:
            app, _ = run_gui(Backend, Path(tmp) / 'DisgaeaD2Recomp-Graphics-Preview',
                             '0.2.3', lambda p:False, preview=True, official=True)
            def widgets(w):
                yield w
                for child in w.winfo_children(): yield from widgets(child)
            def button(text):
                return next(w for w in widgets(app) if w.winfo_class()=='Button' and w.cget('text')==text)
            def wait_for(text):
                until = time.monotonic()+3
                while time.monotonic()<until:
                    app.update()
                    if any('text' in w.keys() and w.cget('text')==text for w in widgets(app)): return
                    time.sleep(.01)
                self.fail('Missing UI text: '+text)
            try:
                with patch('update_checker.check_latest', return_value=updates.parse_release(release(), '0.2.3')):
                    button('Verificar atualizações').invoke()
                    wait_for('Nova versão disponível: 0.2.4')
                self.assertEqual(str(button('▶  JOGAR DISGAEA D2')['state']), 'normal')
                _, language = app._launcher_i18n; language('en');app.update()
                with patch('webbrowser.open') as browser:
                    button('Download update on GitHub').invoke()
                    browser.assert_called_once_with(updates.RELEASE_ROOT+'v0.2.4')
                wait_for('New version available: 0.2.4')
                with patch('update_checker.check_latest', side_effect=OSError('offline')):
                    button('Check for updates').invoke()
                    wait_for('Could not check right now. You can play normally.')
                self.assertEqual(str(button('▶  PLAY DISGAEA D2')['state']), 'normal')
            finally:
                app.destroy()
