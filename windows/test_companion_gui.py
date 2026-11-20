import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import tkinter as tk
    root = tk.Tk()
    root.withdraw()
    root.destroy()
    HAVE_TK = True
except Exception:   # no tkinter or no display
    HAVE_TK = False

STATE = ('ok=1\nsupported=1\nverified=1\nready=1\nversion=1.00\nstatus=Ready\nhp_lock=1\nsp_lock=0\none_hit=0\nfree_shop=0\n'
         'exp=4\ngen=3\nunit=0\nparty=1\ncheats_used=1\ng.hl=500\nc.level=12\np.0=Laharl|12|1\n')


class FakeClient:
    connected, pid = True, 0

    def __init__(self):
        self.sent = []

    def request(self, text):
        self.sent.append(text)
        if text == 'state':
            return STATE
        if text.startswith('fields 0'):
            return 'hl|HL|0|999999\n'
        if text.startswith('fields 1'):
            return 'level|Level|1|9999\n'
        return '0'

    def code(self, text):
        self.sent.append(text)
        return 0

    def close(self):
        self.connected = False


@unittest.skipUnless(HAVE_TK, 'needs tkinter and a display')
class CompanionWindowTests(unittest.TestCase):
    def test_window_toast_and_commands(self):
        from companion_gui import CompanionUI
        app = tk.Tk()
        app.withdraw()
        try:
            with tempfile.TemporaryDirectory() as home:
                ui = CompanionUI(app, home, 'en', {})
                ui.client = FakeClient()
                ui.on_connected()
                ui.open_window()
                ui.refresh()
                self.assertIn('ON', ui.toggle_buttons['hp_lock'].cget('text'))
                self.assertIn('500', ui.general_buttons['hl'][0].cget('text'))
                ui.toggle('sp_lock')
                ui.cycle_exp()
                ui.preset(True, 1)
                ui.toggle_lock()
                self.assertTrue(ui.lock)
                self.assertIn('toggle sp_lock 1', ui.client.sent)
                self.assertIn('exp 8', ui.client.sent)
                self.assertIn('preset save 1', ui.client.sent)
                self.assertIn('trophylock 1', ui.client.sent)
                ui.queue_toast(8, False)
                ui.queue_toast(30, True)
                for _ in range(5):
                    app.update()
                ui.close_window()
                self.assertIsNone(ui.window)
        finally:
            app.destroy()


if __name__ == '__main__':
    unittest.main()
