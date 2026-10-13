import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile
from frame_capture import CaptureSession, WinEvent, clean_capture_environment


class FakeEvent:
    def __init__(self, name): self.signals = 0; self.closed = False
    def signal(self): self.signals += 1
    def close(self): self.closed = True


class CaptureTests(unittest.TestCase):
    def test_normal_environment_and_session_requests(self):
        original = {'PATH': 'tools', 'RSX_CAP': '1:100', 'd2_capture_event': 'old', 'D2_CAPTURE_F8': '1'}
        self.assertEqual(clean_capture_environment(original), {'PATH': 'tools'})
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent)
            self.assertNotIn('RSX_CAP', session.environment(original))
            self.assertEqual(session.environment(original)['D2_CAPTURE_MASK_STATE'], '1')
            with self.assertRaises(RuntimeError): session.request()
            session.started(); session.request()
            self.assertEqual(session.event.signals, 1)
            with self.assertRaises(RuntimeError): session.request()
            frame = session.directory / 'frame_00'; frame.mkdir()
            marker = frame / 'complete.json'; marker.write_text('{')
            self.assertEqual(session.results(), [])
            marker.write_text(json.dumps({'schema': 1, 'status': 'complete'}))
            self.assertIn('1/8', session.status())
            (frame / 'hud-split.json').write_text(json.dumps({'status': 'exact-match'}))
            self.assertIn('HUD idêntica em 1', session.status())
            (frame / 'hud-split.json').write_text(json.dumps({'status': 'mismatch'}))
            self.assertNotIn('HUD idêntica', session.status())
            session.request(); self.assertEqual(session.event.signals, 2)
            self.assertIsNotNone(session.finish(destination=directory))
            self.assertTrue(session.event.closed)
            with self.assertRaises(RuntimeError): session.request()

    def test_export_only_own_capture_payload_and_log(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory); session = CaptureSession(home, 'test', {'upscaler': 'fsr1'}, FakeEvent)
            (home / 'EBOOT.elf').write_bytes(b'not in report')
            frame = session.directory / 'frame_00'; frame.mkdir()
            (frame / 'manifest.txt').write_text('ops'); (frame / 'backbuffer.bmp').write_bytes(b'image')
            (frame / 'complete.json').write_text(json.dumps({'schema': 1, 'status': 'failed'}))
            log = home / 'game.log'; log.write_text('exit')
            report = session.finish(log, home)
            with zipfile.ZipFile(report) as archive:
                names = archive.namelist()
                self.assertTrue(any(n.endswith('manifest.txt') for n in names))
                self.assertTrue(any(n.endswith('game.log') for n in names))
                self.assertFalse(any('EBOOT' in n for n in names))
            self.assertFalse(report.with_suffix('.zip.tmp').exists())

    def test_limit_and_no_capture_report(self):
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent); session.started()
            self.assertIsNone(session.finish(destination=directory))
            session = CaptureSession(directory, 'test', {}, FakeEvent); session.started()
            for i in range(8):
                frame = session.directory / f'frame_{i:02d}'; frame.mkdir()
                (frame / 'complete.json').write_text(json.dumps({'schema': 1, 'status': 'complete'}))
            with self.assertRaises(RuntimeError): session.request()
            session.finish(destination=directory)

    @unittest.skipUnless(os.name == 'nt', 'Win32 events')
    def test_real_event_lifetime(self):
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, WinEvent)
            session.started(); session.request(); session.finish(destination=directory)
            self.assertIsNone(session.event.handle)


if __name__ == '__main__': unittest.main()
