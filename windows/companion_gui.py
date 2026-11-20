"""PlayStation-style trophy pop-up for the Windows game, drawn by the launcher on top of the running game. The game serves
a pipe with the trophy events (see port/src/d2_companion_win.cpp). The cheats and the character editor live inside the
game's own F1 panel (port/d2_d3d12_options.inc), so there is a single menu window."""
import ctypes
import ctypes.wintypes
import os
import tkinter as tk
from pathlib import Path

from companion import PipeClient, PipeError, TrophyInfo, png_data, texts

PANEL, INK, MUTED = '#fffbf0', '#2a2140', '#6b6285'
RED, RED_D, BLUE, BLUE_D, PURPLE, PURPLE_D = '#ff5e73', '#d12a3f', '#5b9bff', '#2554c7', '#a27bff', '#6a3fd6'
NAVY, GOLD = '#14193a', '#ffd166'
CUP = {'P': ('#e8f4ff', '#8fb6d8'), 'G': ('#ffd84a', '#c89400'), 'S': ('#e4e8ee', '#8a93a3'), 'B': ('#e49a5c', '#9a5a22')}


def _user32():
    return ctypes.windll.user32 if os.name == 'nt' else None


def game_rect(pid):
    """(left, top, right, bottom) of the biggest visible window of the game process, or None."""
    user32 = _user32()
    if user32 is None or not pid:
        return None
    best = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rect = ctypes.wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            area = (rect.right - rect.left) * (rect.bottom - rect.top)
            if area > 10000:
                best.append((area, hwnd, (rect.left, rect.top, rect.right, rect.bottom)))
        return True
    try:
        user32.EnumWindows(callback_type(visit), 0)
    except (OSError, AttributeError):
        return None
    if not best:
        return None
    best.sort(reverse=True)
    return best[0][2], best[0][1]


def foreground_pid():
    user32 = _user32()
    if user32 is None:
        return 0
    owner = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(owner))
    return owner.value


def rounded(canvas, x1, y1, x2, y2, r, **kw):
    points = [x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y2 - r, x2, y2, x2 - r, y2, x1 + r, y2, x1, y2, x1, y2 - r, x1, y1 + r, x1, y1]
    return canvas.create_polygon(points, smooth=True, **kw)


class Toast:
    """Banner that slides down from the top of the game window, like a PlayStation trophy notice."""
    W, H, HOLD = 560, 112, 3400

    def __init__(self, app):
        self.app = app
        self.window = None

    def show(self, anchor, title, name, sub, blocked, grade, icon_png, done):
        w = tk.Toplevel(self.app)
        self.window = w
        w.overrideredirect(True)
        w.attributes('-topmost', True)
        transparent = '#010203'
        w.configure(bg=transparent)
        try:
            w.attributes('-transparentcolor', transparent)
        except tk.TclError:
            pass
        canvas = tk.Canvas(w, width=self.W, height=self.H, bg=transparent, highlightthickness=0)
        canvas.pack()
        edge = '#ff5e73' if blocked else '#7aa8ff'
        rounded(canvas, 3, 3, self.W - 3, self.H - 3, 26, fill=NAVY, outline=edge, width=2)
        rounded(canvas, 8, 8, self.W - 8, 40, 20, fill='#1f2756', outline='')
        icon = None
        if icon_png:
            try:
                icon = tk.PhotoImage(data=png_data(icon_png))
                factor = max(1, icon.width() // 72)
                icon = icon.subsample(factor, factor)
            except tk.TclError:
                icon = None
        x = 24
        if icon is not None:
            canvas.create_image(x + 38, self.H // 2 + 4, image=icon)
            canvas.image_ref = icon
            x += 90
        self.cup(canvas, x + 28, self.H // 2 + 6, grade, blocked)
        x += 74
        canvas.create_text(x, 26, text=title.upper(), anchor='w', fill='#ff8a99' if blocked else GOLD, font=('Segoe UI', 10, 'bold'))
        canvas.create_text(x, 56, text=name, anchor='w', fill='white', font=('Segoe UI', 15, 'bold'), width=self.W - x - 20)
        canvas.create_text(x, 88, text=sub, anchor='w', fill='#a9b4e8', font=('Segoe UI', 10))
        left, top, right, bottom = anchor
        x0 = left + max(0, (right - left - self.W) // 2)
        y_end = top + 24
        y_start = top - self.H
        w.geometry('%dx%d+%d+%d' % (self.W, self.H, x0, y_start))
        steps = 14

        def slide(i, going_in):
            if not w.winfo_exists():
                return
            t = i / steps
            ease = 1 - (1 - t) ** 3
            y = y_start + (y_end - y_start) * (ease if going_in else 1 - t ** 2)
            w.geometry('+%d+%d' % (x0, int(y)))
            if i < steps:
                w.after(16, slide, i + 1, going_in)
            elif going_in:
                w.after(self.HOLD, slide, 0, False)
            else:
                w.destroy()
                done()
        slide(0, True)

    @staticmethod
    def cup(canvas, cx, cy, grade, blocked):
        light, dark = ('#c9ced8', '#8a93a3') if blocked else CUP.get(grade, CUP['B'])
        canvas.create_polygon(cx - 20, cy - 24, cx + 20, cy - 24, cx + 16, cy + 2, cx + 6, cy + 10, cx - 6, cy + 10, cx - 16, cy + 2,
                              fill=light, outline=dark, width=2)
        canvas.create_arc(cx - 32, cy - 20, cx - 12, cy, start=90, extent=180, style='arc', outline=dark, width=3)
        canvas.create_arc(cx + 12, cy - 20, cx + 32, cy, start=-90, extent=180, style='arc', outline=dark, width=3)
        canvas.create_rectangle(cx - 4, cy + 10, cx + 4, cy + 20, fill=dark, outline='')
        canvas.create_rectangle(cx - 14, cy + 20, cx + 14, cy + 28, fill=dark, outline='')
        canvas.create_oval(cx - 11, cy - 18, cx - 5, cy - 6, fill='white', outline='', stipple='gray50')
        if blocked:
            canvas.create_line(cx - 22, cy - 26, cx + 22, cy + 30, fill='#ff5e73', width=5)


class CompanionUI:
    """Polls the game's pipe for trophy events and shows the pop-up. The trophy lock itself is switched in the game's
    own F1 panel (CHEATS tab), so nothing here changes game settings."""

    def __init__(self, app, home, language, config=None):
        self.app, self.home, self.language = app, Path(home), language
        self.t = texts(language)
        self.config = config or {}
        self.client = PipeClient()
        self.trophies = TrophyInfo(language)
        self.toast = Toast(app)
        self.queue = []
        self.showing = False
        self.tick_count = 0

    def set_language(self, language):
        self.language = language
        self.t = texts(language)
        self.trophies.language = language

    def start(self):
        self.app.after(500, self.tick)

    def tick(self):
        try:
            self.step()
        except Exception:   # a companion problem must never disturb the launcher
            pass
        self.app.after(100, self.tick)

    def step(self):
        self.tick_count += 1
        if not self.client.connected:
            if self.tick_count % 10 == 0 and self.client.connect():
                self.on_connected()
            return
        if self.tick_count % 3 != 0:
            return
        try:
            event = self.client.request('trophy').strip()
        except PipeError:
            self.client.close()
            return
        if event and '|' in event:
            kind, ident = event.split('|', 1)
            self.queue_toast(int(ident), kind == '1')

    def on_connected(self):
        root = self.config.get('game_root') if isinstance(self.config, dict) else None
        self.trophies.load(root, str(self.home / 'TROPHY.TRP'))

    # ------------------------------------------------------------------ trophy pop-up
    def queue_toast(self, trophy_id, blocked, sample=False):
        self.queue.append((trophy_id, blocked))
        if not self.showing:
            self.next_toast()

    def anchor(self):
        found = game_rect(self.client.pid)
        if found:
            return found[0]
        return (0, 0, self.app.winfo_screenwidth(), self.app.winfo_screenheight())

    def next_toast(self):
        if not self.queue:
            self.showing = False
            return
        self.showing = True
        trophy_id, blocked = self.queue.pop(0)
        entry = self.trophies.get(trophy_id)
        t = self.t
        title = t['trophy_blocked'] if blocked else t['trophy_unlocked']
        grade_text = t['grade'].get(entry.grade, '')
        sub = t['trophy_blocked_sub'] if blocked else (grade_text or t['trophy_id'].format(trophy_id))
        self.toast.show(self.anchor(), title, entry.name or t['trophy_id'].format(trophy_id), sub, blocked, entry.grade, entry.icon, self.next_toast)
