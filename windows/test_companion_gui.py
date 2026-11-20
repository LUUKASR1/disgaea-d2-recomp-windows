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

class FakeClient:
    connected, pid = True, 0

    def __init__(self, events):
        self.events = list(events)

    def request(self, text):
        if text == 'trophy':
            return self.events.pop(0) if self.events else 'none'
        return '0'

    def close(self):
        self.connected = False


@unittest.skipUnless(HAVE_TK, 'needs tkinter and a display')
class CompanionToastTests(unittest.TestCase):
    def test_trophy_events_show_the_popup(self):
        from companion_gui import CompanionUI
        app = tk.Tk()
        app.withdraw()
        try:
            with tempfile.TemporaryDirectory() as home:
                ui = CompanionUI(app, home, 'en', {})
                ui.client = FakeClient(['0|8', '1|30', 'none'])
                for _ in range(12):
                    ui.step()
                self.assertTrue(ui.showing)
                for _ in range(5):
                    app.update()
                self.assertEqual(len(ui.queue), 1)   # the second one waits for the first to finish
        finally:
            app.destroy()


if __name__ == '__main__':
    unittest.main()
