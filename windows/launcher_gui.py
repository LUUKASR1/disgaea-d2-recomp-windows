"""Disgaea-themed desktop UI using bundled Tk; no third-party artwork."""
import json
import os
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import webbrowser

from display_settings import DEFAULTS, RESOLUTIONS, load_settings, save_settings

BG, PANEL, CARD = '#101018', '#171722', '#20202c'
TEXT, MUTED, RED, GOLD = '#f5eee3', '#aaa6b5', '#bb3449', '#d4b879'


def run_gui(Backend, home, version, firmware_present, preview=False):
    app = tk.Tk()
    app.title('Disgaea D2 • Windows Launcher • ' + version)
    app.geometry('1040x680')
    app.minsize(920, 640)
    app.configure(bg=BG)
    style = ttk.Style(app)
    style.theme_use('clam')
    style.configure('D2.TCombobox', fieldbackground=CARD, background=CARD, foreground=TEXT,
                    arrowcolor=GOLD, padding=8)
    style.map('D2.TCombobox', fieldbackground=[('readonly', CARD)], foreground=[('readonly', TEXT)])
    style.configure('D2.Horizontal.TProgressbar', background=RED, troughcolor=CARD, bordercolor=CARD)
    app.option_add('*TCombobox*Listbox.background', CARD)
    app.option_add('*TCombobox*Listbox.foreground', TEXT)
    events, buttons, entries = queue.Queue(), [], []
    busy = False
    backend = Backend(lambda message: events.put(('status', message)), home=home)
    config = {}
    try:
        config = json.loads((home / 'build-atual.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        pass
    folder = tk.StringVar(value=config.get('game_root', ''))
    firmware = tk.StringVar()
    firmware_text = tk.StringVar(value='Firmware disponível no auxiliar local.' if firmware_present(home / 'auxiliar-rpcs3') else 'Selecione seu firmware PUP ou importe do RPCS3.')
    packages = []
    package_text = tk.StringVar(value='Nenhum pacote selecionado.')
    ready = bool(config.get('workspace'))
    initial_error = ''
    try:
        settings = load_settings(home)
    except (OSError, ValueError) as error:
        settings, initial_error = dict(DEFAULTS), str(error)
    resolution = tk.StringVar(value=settings['resolution'])
    mode = tk.StringVar(value='Janela' if settings['mode'] == 'windowed' else 'Tela cheia sem bordas')
    vsync = tk.BooleanVar(value=settings['vsync'])
    status = tk.StringVar(value=initial_error or ('Sua build está pronta. Clique em Jogar.' if ready else 'Prepare sua cópia do jogo para começar.'))
    build_status = tk.StringVar(value='BUILD LOCAL DISPONÍVEL' if ready else 'PRIMEIRA PREPARAÇÃO')
    summary = tk.StringVar()

    def label(parent, text='', size=11, color=TEXT, bold=False, variable=None, wrap=640):
        widget = tk.Label(parent, text=text, textvariable=variable, bg=parent['bg'], fg=color,
                          font=('Segoe UI', size, 'bold' if bold else 'normal'), anchor='w', justify='left', wraplength=wrap)
        widget.pack(anchor='w', pady=(0, 8))
        return widget

    def button(parent, text, command, primary=False):
        widget = tk.Button(parent, text=text, command=command, bg=RED if primary else CARD,
                           fg=TEXT, activebackground='#d34259' if primary else '#303042', activeforeground=TEXT,
                           disabledforeground='#747080', bd=0, relief='flat', cursor='hand2',
                           font=('Segoe UI', 12, 'bold' if primary else 'normal'), padx=20, pady=9)
        widget.pack(anchor='w', pady=4)
        buttons.append(widget)
        return widget

    def card(parent):
        widget = tk.Frame(parent, bg=CARD, padx=20, pady=14)
        widget.pack(fill='x', pady=9)
        return widget

    sidebar = tk.Frame(app, bg=PANEL, width=220, padx=22, pady=26)
    sidebar.pack(side='left', fill='y')
    sidebar.pack_propagate(False)
    label(sidebar, 'DISGAEA', 23, GOLD, True)
    label(sidebar, 'D2', 42, TEXT, True)
    label(sidebar, 'A BRIGHTER DARKNESS', 9, MUTED)
    tk.Frame(sidebar, bg=RED, height=3).pack(fill='x', pady=(8, 25))
    main = tk.Frame(app, bg=BG, padx=28, pady=24)
    main.pack(side='right', fill='both', expand=True)
    footer = tk.Frame(main, bg=PANEL, padx=16, pady=12)
    footer.pack(side='bottom', fill='x', pady=(16, 0))
    label(footer, variable=status, size=10, color=MUTED, wrap=670)
    progress = ttk.Progressbar(footer, mode='indeterminate', style='D2.Horizontal.TProgressbar')
    progress.pack(fill='x')
    content = tk.Frame(main, bg=BG)
    content.pack(fill='both', expand=True)
    pages, bodies, canvases, nav_buttons = {}, {}, {}, {}
    active_page = ['Início']

    def show(name):
        for page in pages.values():
            page.pack_forget()
        pages[name].pack(fill='both', expand=True)
        active_page[0] = name
        canvases[name].yview_moveto(0)
        for key, widget in nav_buttons.items():
            widget.configure(bg=RED if key == name else PANEL)

    for name in ('Início', 'Vídeo', 'Preparação', 'Arquivos e suporte'):
        page = tk.Frame(content, bg=BG)
        pages[name] = page
        canvas = tk.Canvas(page, bg=BG, highlightthickness=0)
        scrollbar = tk.Scrollbar(page, command=canvas.yview, width=12, bg=PANEL, troughcolor=BG)
        scrollbar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        body = tk.Frame(canvas, bg=BG)
        item = canvas.create_window((0, 0), window=body, anchor='nw')
        body.bind('<Configure>', lambda event, c=canvas: c.configure(scrollregion=c.bbox('all')))
        canvas.bind('<Configure>', lambda event, c=canvas, i=item: c.itemconfigure(i, width=event.width))
        bodies[name], canvases[name] = body, canvas
        nav = tk.Button(sidebar, text=name, command=lambda n=name: show(n), bg=PANEL, fg=TEXT,
                        activebackground=RED, activeforeground=TEXT, font=('Segoe UI', 11), bd=0,
                        anchor='w', padx=14, pady=14, cursor='hand2')
        nav.pack(fill='x', pady=3)
        nav_buttons[name] = nav
    label(sidebar, 'WINDOWS x64\n' + version + '\nLUUKASR1 • Experimental', 9, MUTED)

    def get_settings():
        return {'resolution': resolution.get(), 'mode': 'windowed' if mode.get() == 'Janela' else 'borderless', 'vsync': vsync.get()}

    def update_summary(*unused):
        display = resolution.get().replace('x', ' × ') if mode.get() == 'Janela' else 'Resolução do monitor'
        summary.set(display + '  •  ' + mode.get() + '  •  VSync ' + ('ligado' if vsync.get() else 'desligado'))
        resolution_combo.configure(state='disabled' if busy or mode.get() != 'Janela' else 'readonly')

    def persist():
        save_settings(home, get_settings())
        status.set('Configurações salvas. Serão aplicadas na próxima abertura do jogo.')

    def task(operation):
        nonlocal busy
        if busy:
            return
        busy = True
        for widget in buttons + entries:
            widget.configure(state='disabled')
        progress.start(12)
        def execute():
            try:
                operation()
            except Exception as error:
                events.put(('error', str(error)))
            finally:
                events.put(('done', ''))
        threading.Thread(target=execute, daemon=False).start()

    def play():
        try:
            save_settings(home, get_settings())
        except (OSError, ValueError) as error:
            messagebox.showerror('Configurações', str(error))
            return
        task(backend.play)

    page = bodies['Início']
    label(page, 'SEU MUNDO. SUA BUILD.', 10, GOLD, True)
    label(page, 'Disgaea D2', 30, bold=True)
    label(page, 'A Brighter Darkness', 16, MUTED)
    hero = tk.Canvas(page, height=90, bg=CARD, highlightthickness=0)
    hero.pack(fill='x', pady=(4, 12))
    hero.create_polygon(0, 90, 175, 0, 290, 0, 115, 90, fill='#542330', outline='')
    hero.create_polygon(125, 90, 300, 0, 322, 0, 147, 90, fill=RED, outline='')
    hero.create_text(310, 38, text='A próxima batalha começa aqui.', fill=TEXT, anchor='w', font=('Segoe UI', 12, 'bold'))
    hero.create_text(310, 68, text='Configuração local • Saves preservados', fill=MUTED, anchor='w', font=('Segoe UI', 10))
    info = card(page)
    label(info, variable=build_status, size=10, color=GOLD, bold=True)
    label(info, variable=summary, size=11)
    label(info, 'As opções de vídeo são aplicadas ao abrir o jogo.', 10, MUTED)
    button(page, '▶  JOGAR', play, True)
    button(page, 'Configurar vídeo', lambda: show('Vídeo'))
    button(page, 'Preparar ou importar meu jogo', lambda: show('Preparação'))

    page = bodies['Vídeo']
    label(page, 'EXIBIÇÃO', 10, GOLD, True)
    label(page, 'Do seu jeito', 28, bold=True)
    label(page, 'Escolha como o jogo aparece na tela. Aplique antes de jogar.', color=MUTED)
    video = card(page)
    label(video, 'Modo de exibição', bold=True)
    mode_combo = ttk.Combobox(video, textvariable=mode, values=('Janela', 'Tela cheia sem bordas'), state='readonly', style='D2.TCombobox', width=32)
    mode_combo.pack(anchor='w', pady=(0, 18)); entries.append(mode_combo)
    label(video, 'Resolução de exibição em janela', bold=True)
    resolution_combo = ttk.Combobox(video, textvariable=resolution, values=RESOLUTIONS, state='readonly', style='D2.TCombobox', width=32)
    resolution_combo.pack(anchor='w', pady=(0, 10)); entries.append(resolution_combo)
    label(video, 'Em tela cheia sem bordas, usa o tamanho do monitor. Janelas maiores que a área disponível são ajustadas para caber.', 10, MUTED)
    check = tk.Checkbutton(video, text='VSync — reduzir cortes na imagem', variable=vsync, bg=CARD, fg=TEXT,
                           selectcolor=PANEL, activebackground=CARD, activeforeground=TEXT, font=('Segoe UI', 11), anchor='w')
    check.pack(anchor='w', pady=8); entries.append(check)
    label(video, 'Padrão: VSync desligado, como na build que você já utiliza. Ligar pode alterar o ritmo dos quadros.', 10, MUTED)
    label(page, 'A resolução de exibição amplia a imagem. Não aumenta a resolução interna nem adiciona detalhes ao jogo.', 10, MUTED)
    def save_video():
        try:
            persist()
        except (OSError, ValueError) as error:
            messagebox.showerror('Configurações', str(error))
    button(page, 'Salvar configurações', save_video, True)
    def reset():
        resolution.set(DEFAULTS['resolution']); mode.set('Janela'); vsync.set(DEFAULTS['vsync'])
        save_video()
    button(page, 'Restaurar padrão', reset)

    page = bodies['Preparação']
    label(page, 'PRIMEIRA VEZ E IMPORTAÇÃO', 10, GOLD, True)
    label(page, 'Prepare sua cópia', 28, bold=True)
    label(page, 'Jogo USA extraído + atualização 1.40. Os arquivos do jogo não estão incluídos.', 10, MUTED)
    setup = card(page)
    entry = tk.Entry(setup, textvariable=folder, bg=PANEL, fg=TEXT, insertbackground=TEXT, relief='flat', font=('Segoe UI', 10))
    entry.pack(fill='x', ipady=8); entries.append(entry)
    def choose_folder():
        value = filedialog.askdirectory(title='Pasta do jogo ou PS3_GAME')
        if value:
            folder.set(value)
    button(setup, 'Selecionar jogo extraído', choose_folder)
    row = tk.Frame(setup, bg=CARD); row.pack(fill='x')
    def choose_firmware():
        value = filedialog.askopenfilename(title='Firmware PS3UPDAT.PUP', filetypes=[('Firmware PS3', '*.PUP *.pup')])
        if value:
            firmware.set(value); firmware_text.set('Firmware: ' + Path(value).name)
    def import_firmware():
        value = filedialog.askdirectory(title='Pasta do RPCS3 ou dev_flash')
        if value:
            firmware.set(value); firmware_text.set('Importar firmware: ' + value)
    button(row, 'Selecionar firmware PUP', choose_firmware)
    button(row, 'Importar firmware do RPCS3', import_firmware)
    label(setup, variable=firmware_text, size=9, color=MUTED)
    def choose_packages():
        values = filedialog.askopenfilenames(title='Atualização US 1.40 e DLCs', filetypes=[('Pacotes PS3', '*.pkg')])
        if values:
            packages[:] = values
            package_text.set(str(len(values)) + ' pacote(s) selecionado(s). Serão importados na preparação.')
    button(setup, 'Selecionar atualização / DLCs (.pkg)', choose_packages)
    label(setup, variable=package_text, size=9, color=MUTED, wrap=620)
    def prepare():
        game, pkgs, fw = folder.get(), list(packages), firmware.get()
        def operation():
            backend.prepare(game, pkgs, fw)
            events.put(('prepared', ''))
        task(operation)
    button(page, 'Preparar meu jogo', prepare, True)

    page = bodies['Arquivos e suporte']
    label(page, 'TUDO EM UM LUGAR', 10, GOLD, True)
    label(page, 'Arquivos e suporte', 28, bold=True)
    label(page, 'Sua build, configuração e saves ficam na pasta local do aplicativo.', color=MUTED)
    def open_path(path):
        path.mkdir(parents=True, exist_ok=True)
        os.startfile(path)
    button(page, 'Abrir pasta de saves', lambda: open_path(home / 'auxiliar-rpcs3/dev_hdd0/home'))
    button(page, 'Abrir pasta local do aplicativo', lambda: open_path(home))
    button(page, 'Abrir logs do jogo', lambda: open_path(home / 'logs'))
    button(page, 'Abrir projeto no GitHub', lambda: webbrowser.open('https://github.com/LUUKASR1/disgaea-d2-recomp-windows'))
    button(page, 'Página oficial do firmware PS3', lambda: webbrowser.open('https://www.playstation.com/pt-br/support/hardware/ps3/system-software/'))
    def install():
        if messagebox.askokcancel('Ferramentas de preparação', 'Instalar Git e ferramentas C++ da Microsoft? O download pode ocupar vários GB. Os instaladores oficiais podem apresentar termos e pedir autorização.'):
            task(backend.install_tools)
    button(page, 'Instalar ferramentas', install)
    label(page, 'Não apague a pasta local para atualizar o launcher. Android ainda não está disponível.', 10, MUTED)
    for variable in (resolution, mode, vsync):
        variable.trace_add('write', update_summary)
    update_summary()
    app.bind_all('<MouseWheel>', lambda event: canvases[active_page[0]].yview_scroll(-int(event.delta / 120), 'units'))

    def poll():
        nonlocal busy, ready
        while not events.empty():
            kind, message = events.get()
            if kind == 'done':
                busy = False
                progress.stop()
                for widget in buttons + entries:
                    widget.configure(state='readonly' if isinstance(widget, ttk.Combobox) else 'normal')
                update_summary()
            elif kind == 'prepared':
                ready = True; firmware.set(''); packages.clear()
                package_text.set('Pacotes importados. Não precisam ser selecionados novamente.')
                firmware_text.set('Firmware disponível no auxiliar local.' if firmware_present(home / 'auxiliar-rpcs3') else 'Build pronta para jogar.')
                build_status.set('BUILD LOCAL DISPONÍVEL')
                status.set('Preparação concluída. Clique em Jogar na aba Início.')
            else:
                status.set(message)
                if kind == 'error':
                    messagebox.showerror('Disgaea D2', message)
        app.after(200, poll)

    def close():
        if busy:
            messagebox.showinfo('Aguarde', 'Encerre o jogo ou aguarde a preparação antes de fechar o launcher.')
        else:
            app.destroy()
    app.protocol('WM_DELETE_WINDOW', close)
    show('Início')
    poll()
    if preview:
        return app, show
    app.mainloop()
