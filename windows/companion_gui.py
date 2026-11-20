"""Cheats window and PlayStation-style trophy pop-up for the Windows game, drawn by the launcher on top of the running
game (the game itself is not modified: it only serves a pipe, see port/src/d2_companion_win.cpp). F1 opens the window
while the game is in front. The look follows the launcher/Android menu: light panel, chunky coloured buttons."""
import ctypes
import ctypes.wintypes
import json
import os
import tkinter as tk
from pathlib import Path

from companion import (EXP_STEPS, PipeClient, PipeError, TrophyInfo, extract_trp_from_iso, field_label, parse_fields,
                       parse_state, png_data, texts)

PANEL, INK, MUTED = '#fffbf0', '#2a2140', '#6b6285'
RED, RED_D, BLUE, BLUE_D, PURPLE, PURPLE_D = '#ff5e73', '#d12a3f', '#5b9bff', '#2554c7', '#a27bff', '#6a3fd6'
NAVY, GOLD = '#14193a', '#ffd166'
CUP = {'P': ('#e8f4ff', '#8fb6d8'), 'G': ('#ffd84a', '#c89400'), 'S': ('#e4e8ee', '#8a93a3'), 'B': ('#e49a5c', '#9a5a22')}
VK_F1 = 0x70


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
    def __init__(self, app, home, language, config=None):
        self.app, self.home, self.language = app, Path(home), language
        self.t = texts(language)
        self.config = config or {}
        self.client = PipeClient()
        self.trophies = TrophyInfo(language)
        self.toast = Toast(app)
        self.queue = []
        self.showing = False
        self.window = None
        self.state = {'ok': False, 'party': [], 'g': {}, 'c': {}}
        self.general_fields, self.character_fields = [], []
        self.lock = False
        self.f1_down = False
        self.tick_count = 0
        self.unit = -1

    def set_language(self, language):
        self.language = language
        self.t = texts(language)
        self.trophies.language = language

    # ------------------------------------------------------------------ loop
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
        try:
            if self.hotkey_pressed():
                self.toggle_window()
            if self.tick_count % 3 == 0:
                event = self.client.request('trophy').strip()
                if event and '|' in event:
                    kind, ident = event.split('|', 1)
                    self.queue_toast(int(ident), kind == '1')
            if self.window is not None and self.tick_count % 4 == 0:
                self.refresh()
        except PipeError:
            self.on_disconnected()

    def hotkey_pressed(self):
        user32 = _user32()
        if user32 is None:
            return False
        down = bool(user32.GetAsyncKeyState(VK_F1) & 0x8000)
        pressed = down and not self.f1_down
        self.f1_down = down
        return pressed and foreground_pid() in (self.client.pid, os.getpid())

    def on_connected(self):
        self.lock = self.saved_lock()
        self.client.request('trophylock %d' % (1 if self.lock else 0))
        self.general_fields = parse_fields(self.client.request('fields 0'))
        self.character_fields = parse_fields(self.client.request('fields 1'))
        root = self.config.get('game_root') if isinstance(self.config, dict) else None
        self.trophies.load(root, str(self.home / 'TROPHY.TRP'))

    def on_disconnected(self):
        self.client.close()
        self.close_window()

    # ------------------------------------------------------------------ settings kept next to the launcher's own
    def settings_path(self):
        return self.home / 'companion.json'

    def saved_lock(self):
        try:
            return bool(json.loads(self.settings_path().read_text(encoding='utf-8')).get('trophy_lock'))
        except (OSError, ValueError, AttributeError):
            return False

    def save_lock(self):
        try:
            self.home.mkdir(parents=True, exist_ok=True)
            self.settings_path().write_text(json.dumps({'trophy_lock': self.lock}), encoding='utf-8')
        except OSError:
            pass

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

    # ------------------------------------------------------------------ cheats window
    def toggle_window(self):
        if self.window is not None:
            self.close_window()
        else:
            self.open_window()

    def close_window(self):
        if self.window is not None:
            try:
                self.window.destroy()
            except tk.TclError:
                pass
        self.window = None
        self.give_focus_back()

    def give_focus_back(self):
        user32 = _user32()
        found = game_rect(self.client.pid)
        if user32 is not None and found:
            try:
                user32.SetForegroundWindow(found[1])
            except OSError:
                pass

    def chunky(self, parent, text, command, color=BLUE, dark=BLUE_D, size=11):
        return tk.Button(parent, text=text, command=command, bg=color, fg='white', activebackground=dark, activeforeground='white',
                         relief='raised', bd=4, font=('Segoe UI', size, 'bold'), cursor='hand2', padx=10, pady=6)

    def open_window(self):
        t = self.t
        w = tk.Toplevel(self.app)
        self.window = w
        w.title(t['title'])
        w.configure(bg=PANEL)
        w.attributes('-topmost', True)
        w.protocol('WM_DELETE_WINDOW', self.close_window)
        w.bind('<F1>', lambda e: self.close_window())
        w.bind('<Escape>', lambda e: self.close_window())
        left, top, right, bottom = self.anchor()
        width, height = 780, 600
        w.geometry('%dx%d+%d+%d' % (width, height, left + max(0, (right - left - width) // 2), top + max(0, (bottom - top - height) // 2)))
        header = tk.Frame(w, bg=PANEL)
        header.pack(fill='x', padx=16, pady=(12, 4))
        tk.Label(header, text=t['title'], bg=PANEL, fg=RED, font=('Segoe UI', 15, 'bold')).pack(side='left')
        self.chunky(header, t['close'], self.close_window, RED, RED_D, 10).pack(side='right')
        self.status = tk.Label(w, text=t['waiting'], bg=PANEL, fg=MUTED, font=('Segoe UI', 10), anchor='w')
        self.status.pack(fill='x', padx=18)
        tabs = tk.Frame(w, bg=PANEL)
        tabs.pack(fill='x', padx=16, pady=8)
        self.pages, self.tab_buttons = {}, {}
        body = tk.Frame(w, bg=PANEL)
        body.pack(fill='both', expand=True, padx=16, pady=(0, 12))
        for key in ('cheats', 'party', 'trophies'):
            button = self.chunky(tabs, t['tab_' + key], lambda k=key: self.show_page(k), BLUE, BLUE_D)
            button.pack(side='left', padx=(0, 8))
            self.tab_buttons[key] = button
            page = tk.Frame(body, bg=PANEL)
            self.pages[key] = page
        self.build_cheats(self.pages['cheats'])
        self.build_party(self.pages['party'])
        self.build_trophies(self.pages['trophies'])
        self.show_page('cheats')
        self.refresh()
        w.focus_force()

    def show_page(self, key):
        for name, page in self.pages.items():
            page.pack_forget()
            self.tab_buttons[name].configure(bg=BLUE, relief='raised')
        self.pages[key].pack(fill='both', expand=True)
        self.tab_buttons[key].configure(bg=RED, relief='sunken')

    # -- Cheats tab
    def build_cheats(self, page):
        t = self.t
        row = tk.Frame(page, bg=PANEL)
        row.pack(fill='x')
        self.toggle_buttons = {}
        for index, name in enumerate(('hp_lock', 'sp_lock', 'one_hit', 'free_shop')):
            button = self.chunky(row, t[name], lambda n=name: self.toggle(n), PURPLE, PURPLE_D)
            button.grid(row=index // 2, column=index % 2, sticky='ew', padx=4, pady=4)
            self.toggle_buttons[name] = button
        row.columnconfigure(0, weight=1)
        row.columnconfigure(1, weight=1)
        self.exp_button = self.chunky(page, t['exp'].format(1), self.cycle_exp, PURPLE, PURPLE_D)
        self.exp_button.pack(fill='x', padx=4, pady=4)
        presets = tk.Frame(page, bg=PANEL)
        presets.pack(fill='x')
        for slot in range(0, 4):
            label = t['default_preset'] if slot == 0 else str(slot)
            self.chunky(presets, t['preset_load'].format(label), lambda s=slot: self.preset(False, s), BLUE, BLUE_D, 9).grid(row=0, column=slot, sticky='ew', padx=3, pady=2)
            if slot:
                self.chunky(presets, t['preset_save'].format(label), lambda s=slot: self.preset(True, s), BLUE, BLUE_D, 9).grid(row=1, column=slot, sticky='ew', padx=3, pady=2)
            presets.columnconfigure(slot, weight=1)
        self.chunky(page, t['disable_all'], self.disable_all, RED, RED_D).pack(fill='x', padx=4, pady=6)
        self.general_frame = tk.Frame(page, bg=PANEL)
        self.general_frame.pack(fill='both', expand=True)
        self.general_buttons = {}
        for index, (key, label, low, high) in enumerate(self.general_fields):
            button = self.chunky(self.general_frame, field_label(label, self.language), lambda k=key, a=label, lo=low, hi=high: self.edit_general(k, a, lo, hi), BLUE, BLUE_D, 10)
            button.grid(row=index // 2, column=index % 2, sticky='ew', padx=4, pady=3)
            self.general_buttons[key] = (button, label)
        self.general_frame.columnconfigure(0, weight=1)
        self.general_frame.columnconfigure(1, weight=1)

    def toggle(self, name):
        on = not bool(self.state.get(name))
        self.report(self.client.code('toggle %s %d' % (name, 1 if on else 0)))

    def cycle_exp(self):
        current = self.state.get('exp') or 1
        following = EXP_STEPS[(EXP_STEPS.index(current) + 1) % len(EXP_STEPS)] if current in EXP_STEPS else 1
        self.report(self.client.code('exp %d' % following))

    def disable_all(self):
        self.report(self.client.code('disable'))

    def preset(self, save, slot):
        self.report(self.client.code('preset %s %d' % ('save' if save else 'load', slot)))

    def edit_general(self, key, label, low, high):
        value = self.ask_number(field_label(label, self.language), low, high, self.state['g'].get(key, low))
        if value is not None:
            self.report(self.client.code('general %s %d' % (key, value)))

    def report(self, code):
        if code == -1:
            self.status.configure(text=self.t['refused'])
        elif code == -2:
            self.status.configure(text=self.t['not_ready'])

    # -- Characters tab
    def build_party(self, page):
        self.party_list = tk.Listbox(page, width=26, height=18, font=('Segoe UI', 11), bg='white', fg=INK, selectbackground=RED,
                                     selectforeground='white', exportselection=False, relief='flat', highlightthickness=2, highlightbackground=PURPLE)
        self.party_list.pack(side='left', fill='y', padx=(4, 10), pady=4)
        self.party_list.bind('<<ListboxSelect>>', self.pick_unit)
        self.character_frame = tk.Frame(page, bg=PANEL)
        self.character_frame.pack(side='left', fill='both', expand=True)
        self.character_hint = tk.Label(self.character_frame, text=self.t['party_hint'], bg=PANEL, fg=MUTED, font=('Segoe UI', 11))
        self.character_hint.pack(anchor='w')
        self.character_buttons = {}
        grid = tk.Frame(self.character_frame, bg=PANEL)
        grid.pack(fill='both', expand=True)
        for index, (key, label, low, high) in enumerate(self.character_fields):
            button = self.chunky(grid, field_label(label, self.language), lambda k=key, a=label, lo=low, hi=high: self.edit_character(k, a, lo, hi), BLUE, BLUE_D, 9)
            button.grid(row=index // 2, column=index % 2, sticky='ew', padx=3, pady=2)
            self.character_buttons[key] = (button, label)
        grid.columnconfigure(0, weight=1)
        grid.columnconfigure(1, weight=1)

    def pick_unit(self, _event=None):
        selection = self.party_list.curselection()
        if selection:
            self.unit = selection[0]
            self.client.request('select %d' % self.unit)

    def edit_character(self, key, label, low, high):
        if self.unit < 0:
            return
        value = self.ask_number(field_label(label, self.language), low, high, self.state['c'].get(key, low))
        if value is not None:
            self.report(self.client.code('char %d %s %d %d' % (self.unit, key, value, self.state.get('gen', 0))))

    # -- Trophies tab
    def build_trophies(self, page):
        t = self.t
        self.lock_button = self.chunky(page, t['trophy_lock_off'], self.toggle_lock, PURPLE, PURPLE_D)
        self.lock_button.pack(fill='x', padx=4, pady=4)
        self.chunky(page, t['trophy_test'], self.test_toast, BLUE, BLUE_D).pack(fill='x', padx=4, pady=4)
        self.chunky(page, t['trophy_pick'], self.pick_trp, BLUE, BLUE_D).pack(fill='x', padx=4, pady=4)
        self.trophy_info = tk.Label(page, text='', bg=PANEL, fg=MUTED, font=('Segoe UI', 10), justify='left')
        self.trophy_info.pack(anchor='w', padx=6, pady=6)
        self.cheats_used = tk.Label(page, text='', bg=PANEL, fg=MUTED, font=('Segoe UI', 10))
        self.cheats_used.pack(anchor='w', padx=6)
        tk.Label(page, text=t['hint'], bg=PANEL, fg=MUTED, font=('Segoe UI', 9)).pack(anchor='w', padx=6, pady=12)

    def toggle_lock(self):
        self.lock = not self.lock
        self.save_lock()
        self.client.request('trophylock %d' % (1 if self.lock else 0))
        self.refresh_trophy_labels()

    def test_toast(self):
        for ident, blocked in ((0, False), (8, False), (30, True)):
            self.queue_toast(ident, blocked)

    def pick_trp(self):
        from tkinter import filedialog
        path = filedialog.askopenfilename(parent=self.window, title=self.t['pick_title'],
                                          filetypes=[('TROPHY.TRP / ISO', '*.trp *.TRP *.iso *.ISO'), ('*', '*.*')])
        if not path:
            return
        target = str(self.home / 'TROPHY.TRP')
        ok = False
        if path.lower().endswith('.iso'):
            self.home.mkdir(parents=True, exist_ok=True)
            ok = extract_trp_from_iso(path, target)
        else:
            try:
                self.home.mkdir(parents=True, exist_ok=True)
                with open(path, 'rb') as source, open(target, 'wb') as out:
                    out.write(source.read())
                ok = True
            except OSError:
                ok = False
        if ok:
            self.trophies = TrophyInfo(self.language)
            ok = self.trophies.load(None, target)
        self.status.configure(text=self.t['pick_done'] if ok else self.t['pick_fail'])
        self.refresh_trophy_labels()

    def refresh_trophy_labels(self):
        t = self.t
        self.lock_button.configure(text=t['trophy_lock_on'] if self.lock else t['trophy_lock_off'])
        self.trophy_info.configure(text=t['trophy_info'].format(self.trophies.count(), self.trophies.named()))
        self.cheats_used.configure(text=t['cheats_used'].format(t['yes'] if self.state.get('cheats_used') else t['no']))

    # -- shared
    def refresh(self):
        if self.window is None:
            return
        try:
            self.state = parse_state(self.client.request('state'))
        except PipeError:
            self.on_disconnected()
            return
        s, t = self.state, self.t
        self.status.configure(text=(s.get('status') or t['waiting']) if s['ok'] else t['waiting'])
        for name, button in self.toggle_buttons.items():
            on = bool(s.get(name))
            button.configure(text='%s: %s' % (t[name], t['on'] if on else t['off']), bg=RED if on else PURPLE)
        self.exp_button.configure(text=t['exp'].format(s.get('exp') or 1))
        for key, (button, label) in self.general_buttons.items():
            button.configure(text='%s: %s' % (field_label(label, self.language), s['g'].get(key, '-')))
        names = ['%s  Lv %d' % (name, level) for name, level, _ in s['party']]
        if list(self.party_list.get(0, 'end')) != names:
            self.party_list.delete(0, 'end')
            for item in names:
                self.party_list.insert('end', item)
            if 0 <= self.unit < len(names):
                self.party_list.selection_set(self.unit)
        for key, (button, label) in self.character_buttons.items():
            value = s['c'].get(key, '-') if s.get('unit', -1) >= 0 else '-'
            button.configure(text='%s: %s' % (field_label(label, self.language), value))
        self.refresh_trophy_labels()

    def ask_number(self, title, low, high, initial):
        t = self.t
        dialog = tk.Toplevel(self.window)
        dialog.title(title)
        dialog.configure(bg=PANEL)
        dialog.attributes('-topmost', True)
        dialog.transient(self.window)
        tk.Label(dialog, text=title, bg=PANEL, fg=INK, font=('Segoe UI', 12, 'bold')).pack(padx=18, pady=(14, 2))
        tk.Label(dialog, text=t['value_prompt'].format(low, high), bg=PANEL, fg=MUTED, font=('Segoe UI', 10)).pack(padx=18)
        entry = tk.Entry(dialog, font=('Segoe UI', 14), justify='center', width=18)
        entry.insert(0, str(initial))
        entry.pack(padx=18, pady=10)
        result = []

        def accept(_event=None):
            try:
                value = int(entry.get().strip())
            except ValueError:
                return
            result.append(max(low, min(high, value)))
            dialog.destroy()
        buttons = tk.Frame(dialog, bg=PANEL)
        buttons.pack(pady=(0, 14))
        self.chunky(buttons, t['ok'], accept, RED, RED_D).pack(side='left', padx=6)
        self.chunky(buttons, t['cancel'], dialog.destroy, BLUE, BLUE_D).pack(side='left', padx=6)
        entry.bind('<Return>', accept)
        entry.focus_force()
        entry.selection_range(0, 'end')
        dialog.grab_set()
        dialog.wait_window()
        return result[0] if result else None
