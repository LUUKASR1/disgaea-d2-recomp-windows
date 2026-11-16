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
    def test_sprite_goal_is_explicit_and_scoped_to_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent, capture_goal='sprites')
            self.assertEqual(session.environment({})['D2_CAPTURE_GOAL'], 'sprites')
            self.assertEqual(json.loads((session.directory / 'session.json').read_text())['capture_goal'], 'sprites')
            self.assertEqual(clean_capture_environment(session.environment({'PATH': 'tools'})), {'PATH': 'tools'})
            session.event.close()
            with self.assertRaises(ValueError):
                CaptureSession(directory, 'test', {}, FakeEvent, capture_goal='unknown')

    def test_shader_corpus_is_automatic_and_exported_without_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent, capture_goal='shaders')
            env = session.environment({'PATH': 'tools', 'd2_shader_corpus': 'old', 'D2_CAPTURE_EVENT': 'old'})
            self.assertEqual(env['D2_SHADER_CORPUS'], '1')
            self.assertNotIn('D2_CAPTURE_EVENT', env)
            self.assertNotIn('D2_CAPTURE_MASK_STATE', env)
            self.assertEqual(clean_capture_environment(env), {'PATH': 'tools'})
            self.assertIsNone(json.loads((session.directory / 'session.json').read_text())['image'])
            session.started()
            with self.assertRaises(RuntimeError): session.request()
            folder = session.directory / 'shaders'; folder.mkdir()
            (folder / 'vp_000.hlsl').write_text('synthetic fixture')
            self.assertIn('1/256', session.status())
            report = session.finish(destination=directory)
            with zipfile.ZipFile(report) as archive:
                self.assertTrue(any(p.endswith('/shaders/vp_000.hlsl') for p in archive.namelist()))
            self.assertEqual(session.event.signals, 0)
            self.assertTrue(session.event.closed)

    def test_isolated_draw_capture_uses_explicit_request_without_motion_survey(self):
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent, capture_goal='draw')
            env = session.environment({'D2_SHADER_CORPUS':'1', 'D2_CAPTURE_MASK_STATE':'1'})
            self.assertEqual(env['D2_DRAW_CAPTURE'], '1')
            self.assertNotIn('D2_SHADER_CORPUS', env)
            self.assertNotIn('D2_CAPTURE_MASK_STATE', env)
            self.assertEqual(clean_capture_environment(env), {})
            session.started(); session.request()
            self.assertEqual(session.event.signals, 1)
            frame = session.directory / 'frame_00'; frame.mkdir()
            (frame / 'complete.json').write_text(json.dumps({'schema':1,'status':'complete','captured':1}))
            (frame / 'packet.bin').write_bytes(b'private local draw')
            self.assertEqual(len(session.results()), 1)
            self.assertIsNotNone(session.finish(destination=directory))

    def test_normal_environment_and_session_requests(self):
        original = {'PATH': 'tools', 'RSX_CAP': '1:100', 'd2_capture_event': 'old', 'D2_CAPTURE_F8': '1', 'D2_SCENERY_FEEDBACK_FILE': 'inherited', 'D2_CAPTURE_GOAL': 'sprites'}
        self.assertEqual(clean_capture_environment(original), {'PATH': 'tools'})
        with tempfile.TemporaryDirectory() as directory:
            session = CaptureSession(directory, 'test', {}, FakeEvent)
            self.assertNotIn('RSX_CAP', session.environment(original))
            self.assertEqual(session.environment(original)['D2_SCENERY_FEEDBACK_FILE'], str(Path(directory) / 'captures' / 'scenery-feedback-v1.bin'))
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
            self.assertIn('Replay interno idêntico em 1', session.status())
            (frame / 'presentation.json').write_text(json.dumps({'hud_after_upscale':True,'native_comparison_available':False,'status':'scaled-output'}))
            self.assertIn('HUD na saída em 1',session.status())
            self.assertNotIn('Saída nativa idêntica',session.status())
            (frame / 'presentation.json').write_text(json.dumps({'hud_after_upscale':True,'native_comparison_available':True,'status':'exact-match'}))
            self.assertIn('Saída nativa idêntica em 1',session.status())
            (frame / 'hud-split.json').write_text(json.dumps({'status': 'mismatch'}))
            self.assertNotIn('Replay interno idêntico', session.status())
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
