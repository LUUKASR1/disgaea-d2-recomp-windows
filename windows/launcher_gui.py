"""Disgaea-themed desktop UI using bundled Tk and user-provided branding artwork."""
import json
import os
from pathlib import Path
import queue
import sys
import threading
import tkinter as tk
from tkinter import ttk, filedialog as raw_filedialog, messagebox as raw_messagebox
import webbrowser

from display_settings import DEFAULTS, RESOLUTIONS, RENDER_RESOLUTIONS, REFRESH_RATES, load_settings, save_settings
from launcher_i18n import LANGUAGES, Localizer, Dialogs, load_language, save_language, text_variable_class, widget_bindings

BG, PANEL, CARD = '#130e1d', '#21152c', '#2b1c38'
TEXT, MUTED, RED, GOLD = '#fff2e6', '#b7a7c5', '#c32d55', '#ffb16a'


def run_gui(Backend, home, version, firmware_present, preview=False, official=False):
    translate = Localizer(load_language(home))
    filedialog, messagebox = Dialogs(raw_filedialog, translate), Dialogs(raw_messagebox, translate)
    TextVariable = text_variable_class(tk, translate)
    app = tk.Tk()
    standalone = home.name in ('DisgaeaD2Recomp-Standalone', 'DisgaeaD2Recomp-Graphics-Preview')
    clean = standalone or home.name == 'DisgaeaD2Recomp-ISO-Clean'
    title_source = 'Disgaea D2 • ' + ('Windows • Stable • ' + version if official else ('Standalone • ' if standalone else 'Teste ISO limpo • ' if clean else 'Windows Launcher • ') + version)
    app.title(translate(title_source))
    app.geometry('1040x680')
    app.minsize(920, 640)
    app.configure(bg=BG)
    assets = (Path(sys._MEIPASS) / 'windows/assets' if getattr(sys, 'frozen', False)
              else Path(__file__).resolve().parent / 'assets')
    logo_image = tk.PhotoImage(file=str(assets / 'logo-winport.png'))
    icon_image = tk.PhotoImage(file=str(assets / 'launcher-icon.png'))
    sidebar_image = icon_image.subsample(2, 2)
    app._branding_images = (logo_image, icon_image, sidebar_image)
    app.iconphoto(True, icon_image)
    if os.name == 'nt':
        app.iconbitmap(default=str(assets / 'launcher.ico'))
    style = ttk.Style(app)
    style.theme_use('clam')
    style.configure('D2.TCombobox', fieldbackground=CARD, background=CARD, foreground=TEXT,
                    arrowcolor=GOLD, padding=8)
    style.map('D2.TCombobox', fieldbackground=[('readonly', CARD)], foreground=[('readonly', TEXT)])
    style.configure('D2.Vertical.TScrollbar', background=CARD, troughcolor=BG, arrowcolor=GOLD, bordercolor=BG, lightcolor=CARD, darkcolor=PANEL)
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
    inputs = {}
    try:
        candidate = json.loads((home / 'preparacao-inputs.json').read_text(encoding='utf-8'))
        if (isinstance(candidate, dict) and isinstance(candidate.get('game_root'), str) and
                isinstance(candidate.get('firmware'), str) and isinstance(candidate.get('packages'), list) and
                all(isinstance(value, str) for value in candidate['packages'])):
            inputs = candidate
    except (OSError, ValueError):
        pass
    folder = tk.StringVar(value=inputs.get('game_root', config.get('game_root', '')))
    firmware = tk.StringVar(value=inputs.get('firmware', ''))
    firmware_text = TextVariable(value='Firmware disponível no auxiliar local.' if firmware_present(home / 'auxiliar-rpcs3') else 'Firmware: resolvido durante a primeira preparação.')
    packages = list(inputs.get('packages', []))
    package_text = TextVariable(value=' • '.join(Path(value).name for value in packages) or 'Nenhum pacote selecionado.')
    ready = bool(config.get('workspace'))
    initial_error = ''
    try:
        settings = load_settings(home)
    except (OSError, ValueError) as error:
        settings, initial_error = dict(DEFAULTS), str(error)
    resolution = tk.StringVar(value=settings['resolution'])
    graphics_api = tk.StringVar(value='d3d12' if official else settings['graphics_api'])
    mode = TextVariable(choice=True, value='Janela' if settings['mode'] == 'windowed' else 'Tela cheia sem bordas')
    vsync = tk.BooleanVar(value=settings['vsync'])
    refresh_rate = TextVariable(choice=True, value='Nativo' if settings['refresh_rate']=='native' else settings['refresh_rate']+' Hz')
    fps_unlimited = tk.BooleanVar(value=settings['fps_unlimited'])
    vrr = tk.BooleanVar(value=settings['vrr'])
    render_resolution = tk.StringVar(value=settings['render_resolution'])
    upscaler = TextVariable(choice=True, value='FSR 1 — espacial' if settings['upscaler'] == 'fsr1' else 'Desligado')
    sharpness = tk.DoubleVar(value=settings['fsr_sharpness'])
    hud_after_upscale = tk.BooleanVar(value=settings['hud_after_upscale'])
    capture_enabled = tk.BooleanVar(value=False)
    capture_goal = TextVariable(choice=True, value="Cenário — girar câmera")
    capture_status = TextVariable(value='Captura desativada.')
    status = TextVariable(value=initial_error or ('Sua build está pronta. Clique em Jogar.' if ready else 'Clique em Jogar e selecione os arquivos da sua cópia.'))
    build_status = TextVariable(value='BUILD LOCAL DISPONÍVEL' if ready else 'PRIMEIRA PREPARAÇÃO')
    summary = TextVariable()

    def label(parent, text='', size=11, color=TEXT, bold=False, variable=None, wrap=640):
        widget = tk.Label(parent, text=text, textvariable=variable, bg=parent['bg'], fg=color,
                          font=('Segoe UI', size, 'bold' if bold else 'normal'), anchor='w', justify='left', wraplength=wrap)
        widget.pack(anchor='w', pady=(0, 8))
        return widget

    def button(parent, text, command, primary=False, while_playing=False):
        widget = tk.Button(parent, text=text, command=command, bg=RED if primary else CARD,
                           fg=TEXT, activebackground='#d34259' if primary else '#303042', activeforeground=TEXT,
                           disabledforeground='#747080', bd=0, relief='flat', cursor='hand2',
                           font=('Segoe UI', 12, 'bold' if primary else 'normal'), padx=20, pady=9)
        widget.pack(anchor='w', pady=4)
        if not while_playing: buttons.append(widget)
        return widget

    def card(parent):
        widget = tk.Frame(parent, bg=CARD, padx=20, pady=14)
        widget.pack(fill='x', pady=9)
        return widget

    sidebar = tk.Frame(app, bg=PANEL, width=220, padx=22, pady=16)
    sidebar.pack(side='left', fill='y')
    sidebar.pack_propagate(False)
    tk.Label(sidebar, image=sidebar_image, bg=PANEL, bd=0).pack(anchor='center', pady=(0, 8))
    label(sidebar, 'DISGAEA D2', 17, TEXT, True)
    label(sidebar, 'WIN PORT', 12, GOLD, True)
    label(sidebar, 'Idioma / Language', 9, MUTED)
    language_choice = tk.StringVar(value=LANGUAGES[translate.language])
    language_combo = ttk.Combobox(sidebar, textvariable=language_choice,
        values=tuple(LANGUAGES.values()), state='readonly', style='D2.TCombobox', width=19)
    language_combo.pack(anchor='w', pady=(0, 4)); entries.append(language_combo)
    tk.Frame(sidebar, bg=RED, height=3).pack(fill='x', pady=(8, 12))
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
        scrollbar = ttk.Scrollbar(page, command=canvas.yview, orient='vertical', style='D2.Vertical.TScrollbar')
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
                        anchor='w', padx=14, pady=8, cursor='hand2')
        nav.pack(fill='x', pady=3)
        nav_buttons[name] = nav
    label(sidebar, 'WINDOWS x64\n' + version + '\nLUUKASR1 • ' + ('Stable / DX12' if official else 'Experimental'), 9, MUTED)

    def get_settings():
        return {'graphics_api': graphics_api.get(), 'resolution': resolution.get(), 'mode': 'windowed' if mode.get() == 'Janela' else 'borderless', 'vsync': vsync.get(), 'refresh_rate': 'native' if refresh_rate.get()=='Nativo' else refresh_rate.get().split()[0], 'fps_unlimited': fps_unlimited.get(), 'vrr': vrr.get(), 'render_resolution': render_resolution.get(),
                'upscaler': 'fsr1' if upscaler.get().startswith('FSR 1') else 'native',
                'fsr_sharpness': sharpness.get(), 'frame_generation': False,
                'hud_after_upscale': hud_after_upscale.get()}

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
        backend.capture_enabled = capture_enabled.get()
        backend.capture_goal = "draw" if capture_goal.get().startswith("Desenho") else "shaders" if capture_goal.get().startswith("Shaders") else "sprites" if capture_goal.get().startswith("Sprites") else "scenery"
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
        if ready and config.get('iso_path') and not packages and not firmware.get():
            try: save_settings(home, get_settings())
            except (OSError, ValueError) as error:
                messagebox.showerror('Configurações', str(error)); return
            task(backend.play); return
        if clean and not ready:
            play_iso(); return
        try:
            save_settings(home, get_settings())
        except (OSError, ValueError) as error:
            messagebox.showerror('Configurações', str(error))
            return
        if not folder.get():
            choose_folder()
            if not folder.get():
                return
        if not ready and not packages and not (home / ('hdd0/game/BLUS31313/USRDIR/Data/START_7.dat' if standalone else 'auxiliar-rpcs3/dev_hdd0/game/BLUS31313/USRDIR/Data/START_7.dat')).is_file():
            choose_packages()
            if not packages:
                status.set('Selecione a atualização USA 1.40 para a primeira preparação.')
                return
        game, pkgs, fw = folder.get(), list(packages), firmware.get()
        if ready and game == config.get('game_root') and not pkgs and not fw:
            task(backend.play)
        else:
            task(lambda: backend.prepare_and_play(game, pkgs, fw, lambda: events.put(('prepared', ''))))

    def play_iso():
        selected = filedialog.askopenfilename(title='Selecionar ISO de Disgaea D2 USA',
            filetypes=[('Imagem ISO', '*.iso'), ('Todos os arquivos', '*.*')])
        if not selected: return
        try:
            save_settings(home, get_settings())
        except (OSError, ValueError) as error:
            messagebox.showerror('Configurações', str(error)); return
        if not ready:
            if not packages and not (home / ('hdd0/game/BLUS31313/USRDIR/Data/START_7.dat' if standalone else 'auxiliar-rpcs3/dev_hdd0/game/BLUS31313/USRDIR/Data/START_7.dat')).is_file():
                choose_packages()
                if not packages:
                    status.set('Selecione a atualização USA 1.40; DLCs são opcionais.'); return
            task(lambda: backend.prepare_iso_and_play(selected, list(packages), firmware.get(),
                 lambda: events.put(('prepared', ''))))
        else:
            task(lambda: backend.play_iso(selected))

    page = bodies['Início']
    label(page, 'BEM-VINDO AO NETHERWORLD', 10, GOLD, True)
    hero = tk.Canvas(page, height=278, bg=BG, highlightthickness=0)
    hero.pack(fill='x', pady=(0, 6))
    logo_item = hero.create_image(0, 0, image=logo_image, anchor='n')
    hero.bind('<Configure>', lambda event: hero.coords(logo_item, event.width // 2, 0))
    button(page, '▶  JOGAR DISGAEA D2', play, True)
    button(page, 'Selecionar ISO e jogar' if clean else 'Jogar pela ISO (experimental)', play_iso)
    info = card(page)
    label(info, variable=build_status, size=10, color=GOLD, bold=True)
    label(info, variable=summary, size=11)
    button(page, 'Configurar vídeo', lambda: show('Vídeo'))
    button(page, 'Preparar ou importar meu jogo', lambda: show('Preparação'))

    page = bodies['Vídeo']
    label(page, 'EXIBIÇÃO', 10, GOLD, True)
    label(page, 'Do seu jeito', 28, bold=True)
    label(page, 'Escolha como o jogo aparece na tela. Aplique antes de jogar.', color=MUTED)
    video = card(page)
    label(video, 'API gráfica', bold=True)
    api_dx12 = tk.Radiobutton(video, text='DirectX 12', variable=graphics_api, value='d3d12',
        bg=CARD, fg=TEXT, selectcolor=PANEL, activebackground=CARD, activeforeground=TEXT)
    api_dx12.pack(anchor='w'); entries.append(api_dx12)
    if not official:
        api_vulkan = tk.Radiobutton(video, text='Vulkan — em implementação', variable=graphics_api, value='vulkan',
            state='disabled', bg=CARD, fg=MUTED, selectcolor=PANEL, disabledforeground=MUTED)
        api_vulkan.pack(anchor='w', pady=(0, 8))
        label(video, 'Vulkan será habilitado quando o backend do jogo estiver disponível. O teste de inicialização já funciona.', 10, MUTED)
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
    label(video, 'Taxa de atualização do monitor', bold=True)
    refresh_combo = ttk.Combobox(video, textvariable=refresh_rate, values=('Nativo', *(rate+' Hz' for rate in REFRESH_RATES if rate!='native')),
        state='readonly', style='D2.TCombobox', width=32)
    refresh_combo.pack(anchor='w', pady=(0,10)); entries.append(refresh_combo)
    for text, variable in (

        ('Permitir VRR / G-SYNC / FreeSync quando suportado', vrr),):
        toggle = tk.Checkbutton(video, text=text, variable=variable, bg=CARD, fg=TEXT,
            selectcolor=PANEL, activebackground=CARD, activeforeground=TEXT, font=('Segoe UI', 11), anchor='w')
        toggle.pack(anchor='w', pady=8); entries.append(toggle)
    label(video, 'Nativo mantém os Hz atuais do Windows. Uma taxa indisponível mantém o modo atual. A troca pode piscar e é restaurada ao fechar normalmente. A lógica permanece no ritmo original de 60 Hz; o modo sem limite foi suspenso após acelerar o jogo. VRR requer VSync desligado.', 10, MUTED)
    graphics = card(page)
    label(graphics, 'Resolução interna de renderização', bold=True)
    render_combo = ttk.Combobox(graphics, textvariable=render_resolution, values=RENDER_RESOLUTIONS,
                                state='readonly', style='D2.TCombobox', width=32)
    render_combo.pack(anchor='w', pady=(0, 10)); entries.append(render_combo)
    label(graphics, 'native = 1280 × 720. Alterar a resolução interna muda a rasterização na GPU.', 10, MUTED)
    label(graphics, 'Upscaling', bold=True)
    upscale_combo = ttk.Combobox(graphics, textvariable=upscaler, values=('Desligado', 'FSR 1 — espacial'),
                               state='readonly', style='D2.TCombobox', width=32)
    upscale_combo.pack(anchor='w', pady=(0, 10)); entries.append(upscale_combo)
    label(graphics, 'Nitidez FSR', bold=True)
    slider = tk.Scale(graphics, variable=sharpness, from_=0, to=1, resolution=0.05, orient='horizontal',
                      bg=CARD, fg=TEXT, troughcolor=PANEL, highlightthickness=0, length=280)
    slider.pack(anchor='w'); entries.append(slider)
    hud_toggle = tk.Checkbutton(graphics, text='HUD após upscaling — experimental', variable=hud_after_upscale,
        bg=CARD, fg=TEXT, selectcolor=PANEL, activebackground=CARD, activeforeground=TEXT, font=('Segoe UI',11))
    hud_toggle.pack(anchor='w',pady=8);entries.append(hud_toggle)
    label(graphics, 'Desenha a interface na resolução de saída nas cenas reconhecidas. Cenas sem suporte usam o caminho atual. Requer reiniciar o jogo.', 10, MUTED)
    label(graphics, 'DLSS e Frame Generation ainda não estão disponíveis.', 10, MUTED)
    if not official:
        label(graphics, 'Capturas F8 incluem temporal-probe.json com constantes de dois frames.', 10, MUTED)
    label(page, 'As configurações são aplicadas na próxima abertura do jogo.', 10, MUTED)
    def save_video():
        try:
            persist()
        except (OSError, ValueError) as error:
            messagebox.showerror('Configurações', str(error))
    button(page, 'Salvar configurações', save_video, True)
    def reset():
        graphics_api.set(DEFAULTS['graphics_api'])
        resolution.set(DEFAULTS['resolution']); mode.set('Janela'); vsync.set(DEFAULTS['vsync']); refresh_rate.set('Nativo'); fps_unlimited.set(DEFAULTS['fps_unlimited']); vrr.set(DEFAULTS['vrr'])
        render_resolution.set('native'); upscaler.set('Desligado'); sharpness.set(DEFAULTS['fsr_sharpness'])
        hud_after_upscale.set(False)
        save_video()
    button(page, 'Restaurar padrão', reset)

    page = bodies['Preparação']
    label(page, 'PRIMEIRA VEZ E IMPORTAÇÃO', 10, GOLD, True)
    label(page, 'Arquivos da sua cópia', 28, bold=True)
    label(page, 'ISO USA + atualização 1.40 em PKG; DLCs opcionais. Sem firmware.' if standalone else 'Jogo USA extraído + atualização 1.40. Os arquivos do jogo não estão incluídos.', 10, MUTED)
    setup = card(page)
    entry = tk.Entry(setup, textvariable=folder, bg=PANEL, fg=TEXT, insertbackground=TEXT, relief='flat', font=('Segoe UI', 10))
    if not standalone:
        entry.pack(fill='x', ipady=8); entries.append(entry)
    def choose_folder():
        value = filedialog.askdirectory(title='Pasta do jogo ou PS3_GAME')
        if value:
            folder.set(value)
    if not standalone: button(setup, 'Selecionar jogo extraído', choose_folder)
    else: button(setup, 'Selecionar ISO e preparar', play_iso)
    row = tk.Frame(setup, bg=CARD); row.pack(fill='x')
    def choose_firmware():
        value = filedialog.askopenfilename(title='Firmware PS3UPDAT.PUP', filetypes=[('Firmware PS3', '*.PUP *.pup')])
        if value:
            firmware.set(value); firmware_text.set('Firmware: ' + Path(value).name)
    def import_firmware():
        value = filedialog.askdirectory(title='Pasta do RPCS3 ou dev_flash')
        if value:
            firmware.set(value); firmware_text.set('Importar firmware: ' + value)
    if not standalone:
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
    button(page, '▶  PREPARAR E JOGAR', play, True)
    label(page, 'Na primeira execução, as ferramentas são baixadas e a build é criada automaticamente. Depois, Jogar reutiliza sua build local.', 10, MUTED)

    page = bodies['Arquivos e suporte']
    if not standalone: button(page, 'Testar instalação ISO limpa', backend.open_clean_iso_test)
    label(page, 'TUDO EM UM LUGAR', 10, GOLD, True)
    label(page, 'Arquivos e suporte', 28, bold=True)
    label(page, 'Sua build, configuração e saves ficam na pasta local do aplicativo.', color=MUTED)
    def open_path(path):
        path.mkdir(parents=True, exist_ok=True)
        os.startfile(path)
    button(page, 'Abrir pasta de saves', lambda: open_path(home / ('hdd0/home' if standalone else 'auxiliar-rpcs3/dev_hdd0/home')))
    button(page, 'Abrir pasta local do aplicativo', lambda: open_path(home))
    button(page, 'Abrir logs do jogo', lambda: open_path(home / 'logs'))
    capture_button = None
    if not official and home.name == 'DisgaeaD2Recomp-Graphics-Preview':
        diagnostic = card(page)
        label(diagnostic, 'CAPTURAS EXPERIMENTAIS: CENÁRIO, SPRITES E SHADERS', 12, GOLD, True)
        toggle = tk.Checkbutton(diagnostic, text='Habilitar captura na próxima abertura do jogo',
            variable=capture_enabled, bg=CARD, fg=TEXT, selectcolor=PANEL,
            activebackground=CARD, activeforeground=TEXT, font=('Segoe UI', 11))
        toggle.pack(anchor='w', pady=4); entries.append(toggle)
        label(diagnostic, 'Objetivo da captura', 10, MUTED)
        goal_combo = ttk.Combobox(diagnostic, textvariable=capture_goal,
            values=('Cenário — girar câmera', 'Sprites — movimentar personagem', 'Shaders — preparar Vulkan', 'Desenho real — comparar Vulkan'),
            state='readonly', style='D2.TCombobox', width=40)
        goal_combo.pack(anchor='w', pady=4); entries.append(goal_combo)
        label(diagnostic, 'Desenho real: pressione F8 em uma cena parada; a coleta pode pausar por alguns segundos. Shaders: coleta automática, sem F8. Para sprites, pressione F8 ao mover o personagem. Escolha antes de abrir o jogo.', 10, MUTED)

        label(diagnostic, 'Com o jogo aberto, pressione F8 ou clique em Capturar frame. Até oito capturas por sessão. Pode haver uma pausa breve durante a gravação.', 10, MUTED)
        label(diagnostic, 'Em Shaders — preparar Vulkan, a coleta é automática durante o jogo, sem F8. Visite menus, Castle Hallway e uma batalha; feche o jogo para receber o ZIP em Downloads. Esse modo não captura imagens.', 10, MUTED)
        label(diagnostic, 'Nas cenas reconhecidas, o relatório inclui cena sem HUD, HUD isolada, recomposição e comparação de pixels. A imagem normal do jogo continua sendo a referência.', 10, MUTED)
        def request_capture():
            try:
                backend.request_frame_capture()
                capture_status.set('Captura solicitada. Aguarde a gravação…')
            except Exception as error:
                messagebox.showinfo('Captura de frame', str(error))
        capture_button = button(diagnostic, 'Capturar frame', request_capture, while_playing=True)
        label(diagnostic, variable=capture_status, color=GOLD)
        label(diagnostic, 'Ao fechar o jogo, um ZIP será salvo em Downloads. Os arquivos originais também ficam na pasta de capturas.', 10, MUTED)
        button(diagnostic, 'Abrir pasta de capturas', lambda: open_path(home / 'captures'), while_playing=True)
        def open_report_folder():
            report = backend.last_capture_report
            open_path(Path(report).parent if report else Path.home() / 'Downloads')
        button(diagnostic, 'Abrir pasta do relatório', open_report_folder, while_playing=True)
    if official:
        button(page, 'Relatar um problema', lambda: webbrowser.open('https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml'))
    button(page, 'Abrir projeto no GitHub', lambda: webbrowser.open('https://github.com/LUUKASR1/disgaea-d2-recomp-windows'))
    if not standalone: button(page, 'Página oficial do firmware PS3', lambda: webbrowser.open('https://www.playstation.com/pt-br/support/hardware/ps3/system-software/'))
    label(page, 'O compilador e o Git são portáteis e preparados pelo launcher, sem instalação manual.', 10, MUTED)
    label(page, 'Não apague a pasta local para atualizar o launcher. Android ainda não está disponível.', 10, MUTED)
    for variable in (resolution, mode, vsync):
        variable.trace_add('write', update_summary)
    update_summary()
    app.bind_all('<MouseWheel>', lambda event: canvases[active_page[0]].yview_scroll(-int(event.delta / 120), 'units'))

    def poll():
        nonlocal busy, ready, config
        while not events.empty():
            kind, message = events.get()
            if kind == 'done':
                busy = False
                progress.stop()
                for widget in buttons + entries:
                    widget.configure(state='readonly' if isinstance(widget, ttk.Combobox) else 'normal')
                update_summary()
            elif kind == 'prepared':
                config = json.loads((home / 'build-atual.json').read_text(encoding='utf-8'))
                folder.set(config.get('game_root', ''))
                ready = True; firmware.set(''); packages.clear()
                package_text.set('Pacotes importados. Não precisam ser selecionados novamente.')
                firmware_text.set('Firmware disponível no auxiliar local.' if firmware_present(home / 'auxiliar-rpcs3') else 'Build pronta para jogar.')
                build_status.set('BUILD LOCAL DISPONÍVEL')
                status.set('Preparação concluída. Iniciando o jogo…')
            else:
                status.set(message)
                if kind == 'error':
                    messagebox.showerror('Disgaea D2', message)
        if capture_button is not None:
            session = backend.capture_session
            capture_button.configure(state='normal' if session and session.running and not session.closed and session.capture_goal != 'shaders' else 'disabled')
            capture_status.set(backend.frame_capture_status() if session else 'Captura desativada.' if not capture_enabled.get() else 'Abra o jogo para capturar.')
        app.after(200, poll)

    def close():
        if busy:
            messagebox.showinfo('Aguarde', 'Encerre o jogo ou aguarde a preparação antes de fechar o launcher.')
        else:
            app.destroy()
    app.protocol('WM_DELETE_WINDOW', close)
    bindings = widget_bindings(app, translate, excluded=(language_combo,))
    localized_variables = (firmware_text, package_text, mode, refresh_rate, upscaler,
                           capture_goal, capture_status, status, build_status, summary)
    def apply_language():
        for binding in bindings:
            binding()
        for variable in localized_variables:
            variable.refresh()
        app.title(translate(title_source))

    def change_language(language):
        if busy:
            language_choice.set(LANGUAGES[translate.language]); return
        try:
            save_language(home, language)
        except (OSError, ValueError) as error:
            language_choice.set(LANGUAGES[translate.language])
            messagebox.showerror('Configurações', str(error)); return
        translate.language = language
        language_choice.set(LANGUAGES[language])
        apply_language()
    language_combo.bind('<<ComboboxSelected>>', lambda event: change_language(
        next(code for code, name in LANGUAGES.items() if name == language_choice.get())))
    if preview:
        app._launcher_i18n = (translate, change_language)
        app._launcher_get_settings = get_settings
    apply_language()
    show('Início')
    poll()
    if preview:
        return app, show
    app.mainloop()
