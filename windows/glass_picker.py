"""Glass-style ISO picker for the launcher. Tk only, no extra dependencies.

The window is a rounded, borderless panel (the corners use a transparent colour key) over a purple gradient with soft
glowing orbs, so the cards on top read as frosted glass. ISOs found in the usual folders and the ones used before are listed;
the native file dialog stays available behind "Procurar…". The game is never opened here: the picker only returns a path.
"""
import json
import os
from pathlib import Path
import queue
import threading
import time

BG, PANEL, CARD = '#130e1d', '#21152c', '#2b1c38'
TEXT, MUTED, RED, GOLD = '#fff2e6', '#b7a7c5', '#c32d55', '#ffb16a'
KEY = '#010203'                      # colour key: pixels of this colour are see-through (rounded corners)
WIDTH, HEIGHT, RADIUS = 700, 620, 28
MIN_ISO_BYTES = 500 * 1024 * 1024    # the game image is several GB; smaller files are not it
RECENT_FILE = 'iso-recentes.json'
HINTS = ('disgaea', 'd2', 'blus', 'netherworld')


# ---- pure logic (unit tested without a display) -----------------------------------------------------------------

def mix(first, second, amount):
    a = tuple(int(first[i:i + 2], 16) for i in (1, 3, 5))
    b = tuple(int(second[i:i + 2], 16) for i in (1, 3, 5))
    return '#%02x%02x%02x' % tuple(round(a[i] + (b[i] - a[i]) * amount) for i in range(3))


def format_size(size):
    return '%.1f GB' % (size / 1024 ** 3) if size >= 1024 ** 3 else '%d MB' % (size // 1024 ** 2)


def check_iso(path):
    """(ok, message) for a chosen file. Only cheap checks: the preparation step validates the content."""
    try:
        target = Path(path)
        if target.suffix.lower() != '.iso':
            return False, 'Escolha um arquivo .iso.'
        if not target.is_file():
            return False, 'Arquivo não encontrado.'
        size = target.stat().st_size
    except (OSError, ValueError):
        return False, 'Arquivo não encontrado.'
    if size < MIN_ISO_BYTES:
        return False, 'Arquivo pequeno demais para ser a ISO do jogo.'
    return True, 'Pronta para usar • ' + format_size(size)


def looks_like_game(name):
    lowered = name.lower()
    return any(hint in lowered for hint in HINTS)


def load_recent(home):
    try:
        data = json.loads((Path(home) / RECENT_FILE).read_text(encoding='utf-8'))
        values = data.get('recent') if isinstance(data, dict) else None
        return [v for v in values if isinstance(v, str)][:8] if isinstance(values, list) else []
    except (OSError, ValueError):
        return []


def save_recent(home, path):
    home = Path(home)
    values = [path] + [v for v in load_recent(home) if os.path.normcase(v) != os.path.normcase(path)]
    try:
        home.mkdir(parents=True, exist_ok=True)
        temporary = home / (RECENT_FILE + '.tmp')
        temporary.write_text(json.dumps({'schema': 1, 'recent': values[:8]}) + '\n', encoding='utf-8')
        os.replace(temporary, home / RECENT_FILE)
    except OSError:
        pass  # remembering the choice is a convenience; never block the launcher on it


def default_folders():
    home = Path.home()
    folders = [home / name for name in ('Downloads', 'Desktop', 'Documents', 'Games', 'Jogos', 'ISOs', 'Roms', 'PS3')]
    folders.append(home)
    for drive in 'DEFG':
        for name in ('', 'Games', 'Jogos', 'ISOs', 'PS3'):
            folders.append(Path(drive + ':/') / name if name else Path(drive + ':/'))
    return folders


def find_isos(folders, limit=12, budget=2.0):
    """ISO files up to two levels deep in `folders`, game-looking names first, then newest. Time-boxed."""
    found, seen, started = [], set(), time.monotonic()

    def consider(entry):
        try:
            if entry.is_file() and entry.name.lower().endswith('.iso'):
                info = entry.stat()
                key = os.path.normcase(entry.path)
                if info.st_size >= MIN_ISO_BYTES and key not in seen:
                    seen.add(key)
                    found.append((entry.path, info.st_size, info.st_mtime))
        except OSError:
            pass

    def scan(folder, depth):
        if time.monotonic() - started > budget or len(found) > 80:
            return
        try:
            with os.scandir(folder) as entries:
                children = list(entries)[:400]
        except OSError:
            return
        for entry in children:
            consider(entry)
        if depth > 0:
            for entry in children:
                try:
                    if entry.is_dir(follow_symlinks=False) and not entry.name.startswith(('.', '$')) and \
                            entry.name.lower() not in ('windows', 'program files', 'program files (x86)', 'appdata'):
                        scan(entry.path, depth - 1)
                except OSError:
                    pass

    for folder in folders:
        scan(str(folder), 1)
    found.sort(key=lambda item: (not looks_like_game(os.path.basename(item[0])), -item[2]))
    return found[:limit]


# ---- the window ----------------------------------------------------------------------------------------------------

def _rounded_points(x0, y0, x1, y1, r):
    return [x0 + r, y0, x1 - r, y0, x1, y0, x1, y0 + r, x1, y1 - r, x1, y1, x1 - r, y1,
            x0 + r, y1, x0, y1, x0, y1 - r, x0, y0 + r, x0, y0]


class GlassIsoPicker:
    """Modal picker. `result` holds the chosen path (or '') after `wait()`."""

    def __init__(self, parent, translate, home, initial='', browse=None, folders=None):
        import tkinter as tk
        self.tk, self.translate, self.home, self.browse, self.folders = tk, translate, Path(home), browse, folders
        self.result, self.chosen, self.rows, self._drag = '', '', [], None
        self.events = queue.Queue()
        t = translate
        win = self.win = tk.Toplevel(parent)
        win.withdraw()
        win.overrideredirect(True)
        win.configure(bg=KEY)
        try:
            win.attributes('-transparentcolor', KEY)
            win.attributes('-alpha', 0.975)
        except tk.TclError:
            win.configure(bg=BG)         # platforms without colour keys: a plain rectangle
        parent.update_idletasks()
        x = parent.winfo_rootx() + max(0, (parent.winfo_width() - WIDTH) // 2)
        y = parent.winfo_rooty() + max(0, (parent.winfo_height() - HEIGHT) // 2)
        win.geometry('%dx%d+%d+%d' % (WIDTH, HEIGHT, x, y))
        canvas = self.canvas = tk.Canvas(win, width=WIDTH, height=HEIGHT, bg=KEY, highlightthickness=0, bd=0)
        canvas.pack()
        self._paint_background()

        self.close_text = canvas.create_text(WIDTH - 44, 40, text='✕', fill=MUTED, font=('Segoe UI', 14))
        canvas.tag_bind(self.close_text, '<Enter>', lambda e: canvas.itemconfigure(self.close_text, fill=TEXT))
        canvas.tag_bind(self.close_text, '<Leave>', lambda e: canvas.itemconfigure(self.close_text, fill=MUTED))
        canvas.tag_bind(self.close_text, '<Button-1>', lambda e: self.cancel())
        canvas.create_text(56, 44, text=t('ESCOLHER IMAGEM'), fill=GOLD, anchor='w', font=('Segoe UI', 10, 'bold'))
        canvas.create_text(56, 82, text=t('Sua ISO de Disgaea D2'), fill=TEXT, anchor='w', font=('Segoe UI', 22, 'bold'))
        canvas.create_text(56, 118, text=t('Use a ISO USA do jogo. Ela não é copiada nem alterada.'), fill=MUTED, anchor='w',
                           font=('Segoe UI', 10))
        canvas.bind('<ButtonPress-1>', self._drag_start, add='+')
        canvas.bind('<B1-Motion>', self._drag_move, add='+')

        # path card
        card = tk.Frame(canvas, bg=self.glass(0.10), highlightthickness=1, highlightbackground=self.glass(0.30))
        canvas.create_window(WIDTH // 2, 190, window=card, width=WIDTH - 100, height=76)
        tk.Label(card, text=t('ARQUIVO SELECIONADO'), bg=card['bg'], fg=MUTED, font=('Segoe UI', 8, 'bold')).place(x=16, y=8)
        self.path_var = tk.StringVar(value=initial)
        self.path_label = tk.Label(card, textvariable=self.path_var, bg=card['bg'], fg=TEXT, anchor='w',
                                   font=('Segoe UI', 11), cursor='arrow')
        self.path_label.place(x=16, y=30, width=WIDTH - 100 - 150)
        self.status_var = tk.StringVar(value='')
        self.status_label = tk.Label(card, textvariable=self.status_var, bg=card['bg'], fg=MUTED, anchor='w', font=('Segoe UI', 9))
        self.status_label.place(x=16, y=52, width=WIDTH - 100 - 150)
        self._button(card, t('Procurar…'), self.browse_file, primary=False).place(x=WIDTH - 100 - 132, y=20, width=116, height=36)

        canvas.create_text(56, 262, text=t('ENCONTRADAS NESTE COMPUTADOR'), fill=MUTED, anchor='w', font=('Segoe UI', 8, 'bold'))
        self.list_frame = tk.Frame(canvas, bg=self.glass(0.05), highlightthickness=1, highlightbackground=self.glass(0.18))
        canvas.create_window(WIDTH // 2, 408, window=self.list_frame, width=WIDTH - 100, height=248)
        self.empty_label = tk.Label(self.list_frame, text=t('Procurando ISOs…'), bg=self.list_frame['bg'], fg=MUTED,
                                    font=('Segoe UI', 10))
        self.empty_label.pack(expand=True)

        self.confirm_button = self._button(canvas, t('Usar esta ISO'), self.confirm, primary=True)
        self.cancel_button = self._button(canvas, t('Cancelar'), self.cancel, primary=False)
        canvas.create_window(WIDTH - 56 - 85, HEIGHT - 50, window=self.confirm_button, width=170, height=42)
        canvas.create_window(WIDTH - 56 - 170 - 14 - 55, HEIGHT - 50, window=self.cancel_button, width=110, height=42)
        self._enable_confirm(False)

        win.bind('<Escape>', lambda e: self.cancel())
        win.bind('<Return>', lambda e: self.confirm())
        win.bind('<Up>', lambda e: self._move(-1))
        win.bind('<Down>', lambda e: self._move(1))
        self.set_path(initial) if initial else None
        self._round_corners()
        win.deiconify()
        win.lift()
        win.update_idletasks()
        try:
            win.focus_force()
            win.grab_set()
        except tk.TclError:
            pass
        threading.Thread(target=self._scan, daemon=True).start()
        self.win.after(60, self._poll)

    # -- drawing -------------------------------------------------------------------------------------------------
    def glass(self, amount):
        """A frosted-glass tone: the panel colour lifted towards white."""
        return mix('#241636', '#ffffff', amount)

    def _paint_background(self):
        canvas = self.canvas
        top, bottom = '#33194f', '#0f0a18'
        r = RADIUS
        for row in range(HEIGHT):
            inset = 0
            if row < r:
                inset = r - int((r * r - (r - row - 0.5) ** 2) ** 0.5)
            elif row >= HEIGHT - r:
                inset = r - int((r * r - (row - (HEIGHT - r) + 0.5) ** 2) ** 0.5)
            canvas.create_line(inset, row, WIDTH - inset, row, fill=mix(top, bottom, row / (HEIGHT - 1)))
        # soft glowing orbs behind the glass
        for cx, cy, radius, color in ((WIDTH - 90, 70, 190, RED), (70, HEIGHT - 120, 210, '#5b3bc4'), (WIDTH // 2, HEIGHT + 20, 170, GOLD)):
            steps = 26
            for i in range(steps):
                f = i / (steps - 1)
                rr = radius * (1 - f)
                base = mix(top, bottom, min(1.0, max(0.0, cy / HEIGHT)))
                canvas.create_oval(cx - rr, cy - rr, cx + rr, cy + rr, outline='', fill=mix(base, color, 0.38 * f * f))
        # glass edge: bright on top, faint at the bottom
        canvas.create_polygon(_rounded_points(2, 2, WIDTH - 3, HEIGHT - 3, RADIUS), smooth=True, fill='',
                              outline=mix(top, '#ffffff', 0.42), width=2)
        canvas.create_line(RADIUS + 6, 4, WIDTH - RADIUS - 6, 4, fill=mix(top, '#ffffff', 0.7))

    def _round_corners(self):
        """Windows 11 can also round and shadow the window itself; harmless elsewhere."""
        if os.name != 'nt':
            return
        try:
            import ctypes
            hwnd = ctypes.windll.user32.GetParent(self.win.winfo_id()) or self.win.winfo_id()
            value = ctypes.c_int(2)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, 33, ctypes.byref(value), ctypes.sizeof(value))
        except (OSError, AttributeError):
            pass

    def _button(self, parent, text, command, primary):
        tk = self.tk
        base = RED if primary else self.glass(0.14)
        hover = '#d94566' if primary else self.glass(0.24)
        button = tk.Label(parent, text=text, bg=base, fg=TEXT, font=('Segoe UI', 11, 'bold' if primary else 'normal'),
                          cursor='hand2', bd=0, padx=14, pady=8)
        button._base, button._hover, button._enabled, button._command = base, hover, True, command
        button.bind('<Enter>', lambda e: button._enabled and button.configure(bg=button._hover))
        button.bind('<Leave>', lambda e: button.configure(bg=button._base if button._enabled else self.glass(0.06)))
        button.bind('<Button-1>', lambda e: button._enabled and button._command())
        return button

    def _enable_confirm(self, enabled):
        button = self.confirm_button
        button._enabled = enabled
        button.configure(bg=button._base if enabled else self.glass(0.06), fg=TEXT if enabled else '#7b6e8c',
                         cursor='hand2' if enabled else 'arrow')

    # -- behaviour -----------------------------------------------------------------------------------------------
    def _drag_start(self, event):
        # only the header drags the window; everything below belongs to the controls
        self._drag = (event.x_root - self.win.winfo_x(), event.y_root - self.win.winfo_y()) if event.y < 140 and event.x < WIDTH - 80 else None

    def _drag_move(self, event):
        if self._drag:
            self.win.geometry('+%d+%d' % (event.x_root - self._drag[0], event.y_root - self._drag[1]))

    def set_path(self, path):
        self.chosen = path
        ok, message = check_iso(path)
        self.path_var.set(self._shorten(path, 50))
        self.status_var.set(('✓  ' if ok else '!  ') + self.translate(message))
        self.status_label.configure(fg='#8fe3a8' if ok else '#ff9a8a')
        self._enable_confirm(ok)
        self._highlight()

    def _shorten(self, path, limit=66):
        return path if len(path) <= limit else path[:22] + ' … ' + path[-(limit - 26):]

    def browse_file(self):
        if not self.browse:
            return
        self.win.grab_release()
        try:
            selected = self.browse(os.path.dirname(self.chosen) if self.chosen else '')
        finally:
            try:
                self.win.grab_set()
                self.win.lift()
                self.win.focus_force()
            except self.tk.TclError:
                pass
        if selected:
            self.set_path(os.path.normpath(selected))

    def _scan(self):
        recent = [p for p in load_recent(self.home) if os.path.isfile(p)]
        items = []
        for path in recent:
            try:
                items.append((path, os.stat(path).st_size, os.stat(path).st_mtime, True))
            except OSError:
                pass
        folders = list(self.folders) if self.folders is not None else default_folders()
        if self.chosen:
            folders.insert(0, Path(self.chosen).parent)
        known = {os.path.normcase(i[0]) for i in items}
        for path, size, mtime in find_isos(folders):
            if os.path.normcase(path) not in known:
                items.append((path, size, mtime, False))
        self.events.put(items)

    def _poll(self):
        try:
            items = self.events.get_nowait()
        except queue.Empty:
            if self.win.winfo_exists():
                self.win.after(80, self._poll)
            return
        self._fill(items)

    def _fill(self, items):
        tk, t = self.tk, self.translate
        for child in self.list_frame.winfo_children():
            child.destroy()
        self.rows = []
        if not items:
            tk.Label(self.list_frame, text=t('Nenhuma ISO encontrada. Use “Procurar…” para escolher o arquivo.'), bg=self.list_frame['bg'],
                     fg=MUTED, font=('Segoe UI', 10), wraplength=WIDTH - 160).pack(expand=True)
            return
        for path, size, mtime, recent in items[:4]:
            self._row(path, size, recent)
        self._highlight()

    def _row(self, path, size, recent):
        tk, t = self.tk, self.translate
        row = tk.Frame(self.list_frame, bg=self.list_frame['bg'], cursor='hand2')
        row.pack(fill='x', padx=8, pady=(6, 0))
        name = os.path.basename(path)
        title = tk.Label(row, text=name if len(name) <= 46 else name[:43] + '…', bg=row['bg'], fg=TEXT, anchor='w',
                         font=('Segoe UI', 11, 'bold'))
        title.grid(row=0, column=0, sticky='w', padx=(12, 0), pady=(7, 0))
        place = tk.Label(row, text=self._shorten(os.path.dirname(path), 58), bg=row['bg'], fg=MUTED, anchor='w', font=('Segoe UI', 9))
        place.grid(row=1, column=0, sticky='w', padx=(12, 0), pady=(0, 7))
        tag = t('Usada antes') if recent else (t('Parece ser o jogo') if looks_like_game(name) else '')
        info = tk.Label(row, text=(tag + '   ' if tag else '') + format_size(size), bg=row['bg'], fg=GOLD if tag else MUTED,
                        font=('Segoe UI', 9, 'bold' if tag else 'normal'))
        info.grid(row=0, column=1, rowspan=2, sticky='e', padx=12)
        row.grid_columnconfigure(0, weight=1)
        widgets = (row, title, place, info)
        for widget in widgets:
            widget.bind('<Button-1>', lambda e, p=path: self.set_path(p))
            widget.bind('<Double-Button-1>', lambda e, p=path: (self.set_path(p), self.confirm()))
            widget.bind('<Enter>', lambda e, r=row, w=widgets: self._paint_row(r, w, True))
            widget.bind('<Leave>', lambda e, r=row, w=widgets: self._paint_row(r, w, False))
        self.rows.append((path, row, widgets))

    def _paint_row(self, row, widgets, hover):
        selected = any(r is row and os.path.normcase(p) == os.path.normcase(self.chosen) for p, r, _ in self.rows)
        color = self.glass(0.24) if selected else (self.glass(0.14) if hover else self.list_frame['bg'])
        for widget in widgets:
            widget.configure(bg=color)

    def _highlight(self):
        for path, row, widgets in self.rows:
            self._paint_row(row, widgets, False)

    def _move(self, step):
        if not self.rows:
            return
        paths = [p for p, _, _ in self.rows]
        try:
            index = [os.path.normcase(p) for p in paths].index(os.path.normcase(self.chosen))
        except ValueError:
            index = -1 if step > 0 else len(paths)
        self.set_path(paths[max(0, min(len(paths) - 1, index + step))])

    def confirm(self):
        if not self.confirm_button._enabled:
            return
        self.result = self.chosen
        save_recent(self.home, self.chosen)
        self._close()

    def cancel(self):
        self.result = ''
        self._close()

    def _close(self):
        try:
            self.win.grab_release()
        except self.tk.TclError:
            pass
        self.win.destroy()

    def wait(self):
        self.win.wait_window()
        return self.result


def ask_iso(parent, translate, home, initial='', browse=None, folders=None):
    """Open the glass picker and return the chosen ISO path ('' when cancelled)."""
    return GlassIsoPicker(parent, translate, home, initial, browse, folders).wait()
