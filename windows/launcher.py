"""Windows first-run assistant. All game-derived output stays on this PC."""
import hashlib
from datetime import datetime
import io
import json
import os
from pathlib import Path
import queue
import re
import runpy
import shutil
import subprocess
import sys
import sysconfig  # Required by CMake's interpreter probes inside the frozen worker.
import threading
import urllib.request
import uuid

VERSION = '0.1.5-preview'
ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))
HOME = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'DisgaeaD2Recomp'
ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'
RPCS3_URL = 'https://github.com/RPCS3/rpcs3-binaries-win/releases/download/build-2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3-v0.0.43-20284-2ba2f75b_win64_msvc.7z'
RPCS3_SHA = '8914810c74508b0e53c6b6cb39d4ed334ecee99639973062d259b1481bfa1720'


class StageFailure(RuntimeError):
    def __init__(self, label, returncode, log):
        self.returncode = returncode
        super().__init__(label + ' falhou (codigo ' + str(returncode) + '). Consulte ' + str(log))


def completed_firmware_install(directory, previous_log_sha, returncode):
    """Recover only the observed native abort after an explicitly completed install."""
    log = Path(directory) / 'log/RPCS3.log'
    if returncode != 3221226505 or not firmware_present(directory) or not log.is_file():
        return False
    if digest(log) == previous_log_sha:
        return False
    text = log.read_text(encoding='utf-8', errors='replace')
    return '--installfw' in text and 'GUI: Successfully installed PS3 firmware version ' in text


def completed_update_install(directory, package, previous_log_sha, returncode):
    """Recover only the observed US 1.40 package completion, not arbitrary DLC failures.

    The caller subsequently decrypts a fresh copy and checks the supported ELF hash.
    """
    directory, package = Path(directory), Path(package)
    log = directory / 'log/RPCS3.log'
    update = directory / 'dev_hdd0/game/BLUS31313'
    if returncode != 3221226505 or not log.is_file():
        return False
    if digest(log) == previous_log_sha:
        return False
    if not all((update / name).is_file() for name in
               ('PARAM.SFO', 'USRDIR/EBOOT.BIN', 'USRDIR/Data/START_7.dat')):
        return False
    text = log.read_text(encoding='utf-8', errors='replace').replace('\\', '/')
    expected = 'GUI: Successfully installed ' + str(package).replace('\\', '/')
    return '--installpkg' in text and bool(re.search(
        re.escape(expected) + r' \(title_id=BLUS31313, title=[^\r\n]*, version=01\.40\)\.', text))


def completed_dlc_install(directory, package, previous_log_sha, returncode):
    """Verify the observed NPUB31321 flag package after the helper's native abort.

    Installation completion is distinct from in-game recognition. Never accept
    arbitrary package payloads or skip the subsequent supported ELF check.
    """
    directory, package = Path(directory), Path(package)
    log = directory / 'log/RPCS3.log'
    if returncode != 3221226505 or not log.is_file() or digest(log) == previous_log_sha:
        return False
    text = log.read_text(encoding='utf-8', errors='replace').replace('\\', '/')
    expected = 'GUI: Successfully installed ' + str(package).replace('\\', '/')
    if '--installpkg' not in text or not re.search(
            re.escape(expected) + r' \(title_id=NPUB31321, title=[^\r\n]*, version=01\.00\)\.', text):
        return False
    entries = re.findall(r"PKG: Entry: type=(0x[0-9A-Fa-f]+), name='([^'\r\n]+)'", text)
    directories = {'USRDIR', 'USRDIR/Data', 'USRDIR/Data/flag'}
    if any((int(kind, 16) & 0xff) not in (3, 4) or
           ((int(kind, 16) & 0xff) == 4 and name not in directories)
           for kind, name in entries):
        return False
    files = {name for kind, name in entries if int(kind, 16) & 0xff == 3}
    flags = {name for name in files if re.fullmatch(r'USRDIR/Data/flag/flag\d{8}\.edat', name)}
    metadata = {'PS3LOGO.DAT', 'PARAM.SFO', 'ICON0.PNG', 'PIC1.PNG'}
    if not flags or 'PARAM.SFO' not in files or files - flags - metadata:
        return False
    target = (directory / 'dev_hdd0/game/NPUB31321').resolve()
    for name in files:
        output = target / name
        if not output.resolve().is_relative_to(target) or not output.is_file() or output.stat().st_size == 0:
            return False
    return True


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def game_root(path):
    path = Path(path).resolve()
    if path.name.upper() == 'PS3_GAME':
        path = path.parent
    if not (path / 'PS3_GAME/USRDIR/EBOOT.BIN').is_file():
        raise ValueError('Selecione a pasta do jogo ou PS3_GAME contendo USRDIR/EBOOT.BIN.')
    return path


def firmware_present(directory):
    """Check the modules used by RPCS3's boot/firmware preflight."""
    flash = Path(directory) / 'dev_flash'
    return all((flash / name).is_file() for name in
               ('sys/external/liblv2.sprx', 'vsh/module/vsh.self'))


def build_source_signature(workspace):
    """Compare shipped runtime/lifter sources, excluding generated game files."""
    source = ROOT / 'payload'
    names = [p.relative_to(source) for directory in ('port', 'patches')
             for p in (source / directory).rglob('*') if p.is_file()]
    checksum = hashlib.sha256()
    for name in sorted(names):
        checksum.update(name.as_posix().encode('utf-8') + b'\0')
        checksum.update(bytes.fromhex(digest(Path(workspace) / name)))
    return checksum.hexdigest()


def download(url, target, sha):
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_suffix(target.suffix + '.part')
    try:
        with urllib.request.urlopen(url, timeout=90) as response, part.open('wb') as f:
            shutil.copyfileobj(response, f)
        if digest(part) != sha:
            raise RuntimeError('O arquivo baixado falhou na verificacao SHA-256.')
        part.replace(target)
    finally:
        part.unlink(missing_ok=True)


def worker_mode():
    """The frozen executable also acts as the bundled Python interpreter."""
    if len(sys.argv) < 2:
        return False
    arg = sys.argv[1]
    if arg not in ('-c', '--version', '-V') and not arg.endswith('.py'):
        return False
    for name, fd in [('stdout', 1), ('stderr', 2)]:
        if getattr(sys, name) is None:
            try:
                setattr(sys, name, io.TextIOWrapper(os.fdopen(os.dup(fd), 'wb'), encoding='utf-8', errors='replace', write_through=True))
            except OSError:
                setattr(sys, name, open(os.devnull, 'w'))
    if arg in ('--version', '-V'):
        print('Python ' + sys.version.split()[0])
    elif arg == '-c':
        sys.argv = ['-c'] + sys.argv[3:]
    else:
        script = Path(arg).resolve()
        sys.argv = sys.argv[1:]
        # Frozen multiprocessing cannot import dynamic __main__ worker functions.
        # Use the lifter's serial path until a dedicated worker module is shipped.
        if script.name == 'ppu_lifter.py' and '--jobs' in sys.argv:
            sys.argv[sys.argv.index('--jobs') + 1] = '1'
        sys.path.insert(0, str(script.parent))
        runpy.run_path(str(script), run_name='__main__')
    return True


def build_environment():
    env = dict(os.environ)
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        raise RuntimeError('Instale as ferramentas pelo botao "Instalar ferramentas" e reinicie o aplicativo.')
    install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not install:
        raise RuntimeError('As ferramentas C++ do Visual Studio ainda nao estao instaladas.')
    vcvars = Path(install) / 'VC/Auxiliary/Build/vcvarsall.bat'
    # Pass CMD's command line directly: list2cmdline uses C-runtime escaping,
    # which CMD does not understand for the nested vcvarsall quotes.
    command = 'cmd.exe /d /s /c ""' + str(vcvars) + '" x64 >nul && set"'
    output = subprocess.check_output(command, text=True, errors='replace', timeout=90,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
    for line in output.splitlines():
        name, sep, value = line.partition('=')
        if sep and name:
            env[name] = value
    extra = [ROOT / 'vendor/bin', ROOT / 'vendor/cmake/bin', Path(install) / 'VC/Tools/Llvm/x64/bin', Path(install) / 'VC/Tools/Llvm/bin', Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/cmd']
    env['PATH'] = os.pathsep.join(map(str, extra)) + os.pathsep + env.get('Path', env.get('PATH', ''))
    # Windows environment keys are case insensitive. Avoid duplicate Path/PATH.
    env.pop('Path', None)
    for tool in ('git', 'cmake', 'ninja', 'clang-cl'):
        if not shutil.which(tool, path=env['PATH']):
            raise RuntimeError('Ferramenta ausente: ' + tool + '. Use "Instalar ferramentas".')
    return env


class Backend:
    def __init__(self, notify, home=HOME):
        self.home = Path(home)
        self.notify = notify

    def run(self, args, env=None, timeout=None, cwd=None, label=None, log_path=None):
        self.home.mkdir(parents=True, exist_ok=True)
        log = Path(log_path) if log_path else self.home / 'preparacao.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        args = list(map(str, args))
        label = label or Path(args[0]).name
        self.notify('Executando: ' + label)
        with log.open('ab') as f:
            def record(message):
                f.write((datetime.now().isoformat(timespec='seconds') + ' ' + message + '\n').encode('utf-8'))
                f.flush()
            record('launcher=' + VERSION + ' etapa=' + label)
            record('comando=' + subprocess.list2cmdline(args))
            record('pasta=' + str(cwd or Path.cwd()))
            try:
                result = subprocess.run(args, env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            except subprocess.TimeoutExpired:
                record('resultado=timeout limite=' + str(timeout) + ' segundos')
                raise RuntimeError(label + ' excedeu o tempo limite. Consulte ' + str(log)) from None
            except OSError as error:
                record('resultado=erro ao iniciar: ' + str(error))
                raise RuntimeError('Nao foi possivel iniciar ' + label + ': ' + str(error)) from error
            record('codigo_saida=' + str(result.returncode))
        if result.returncode:
            raise StageFailure(label, result.returncode, log)

    def ensure_firmware(self, helper, firmware):
        directory = helper.parent
        if firmware:
            source = Path(firmware).resolve()
            if source.is_file() and source.suffix.lower() == '.pup':
                rpc_log = directory / 'log/RPCS3.log'
                previous_sha = digest(rpc_log) if rpc_log.is_file() else None
                try:
                    self.run([helper, '--headless', '--installfw', source], timeout=1800,
                             cwd=directory, label='Instalar firmware PS3')
                except StageFailure as error:
                    if not completed_firmware_install(directory, previous_sha, error.returncode):
                        raise
                    message = 'Firmware instalado e modulos presentes; auxiliar encerrou com erro apos confirmar a instalacao.'
                    with (self.home / 'preparacao.log').open('a', encoding='utf-8') as log:
                        log.write(message + '\n')
                    self.notify(message)
            elif source.is_dir():
                flash = source if source.name.lower() == 'dev_flash' else source / 'dev_flash'
                if not firmware_present(flash.parent):
                    raise RuntimeError('A pasta selecionada nao contem um dev_flash reconhecido.')
                target = directory / 'dev_flash'
                if flash.resolve() != target.resolve():
                    staging = directory / ('firmware-' + uuid.uuid4().hex)
                    self.notify('Copiando seu firmware instalado para o auxiliar local.')
                    shutil.copytree(flash, staging / 'dev_flash')
                    if not firmware_present(staging):
                        raise RuntimeError('A copia do firmware ficou incompleta; a instalacao anterior foi preservada.')
                    if target.exists():
                        target.rename(directory / ('dev_flash-anterior-' + uuid.uuid4().hex))
                    (staging / 'dev_flash').rename(target)
                    staging.rmdir()
            else:
                raise ValueError('Selecione um PS3UPDAT.PUP ou a pasta do seu RPCS3 com firmware instalado.')
        if not firmware_present(directory):
            raise RuntimeError('Firmware ausente. Selecione PS3UPDAT.PUP ou importe a pasta do seu RPCS3 antes de preparar.')

    def cached_build(self):
        cache = self.home / 'build-atual.json'
        if not cache.is_file():
            return None
        try:
            previous = json.loads(cache.read_text(encoding='utf-8'))
            repo = Path(previous['workspace'])
            if not (repo / 'port/build-windows-140/DisgaeaD2Recomp.exe').is_file():
                return None
            if digest(repo / 'work/v140/EBOOT.elf') != ELF_SHA:
                return None
            update = Path(previous['hdd0_root']) / 'game/BLUS31313/USRDIR'
            if not (update / 'Data/START_7.dat').is_file() or not (update / 'EBOOT.BIN').is_file():
                return None
            if build_source_signature(repo) != build_source_signature(ROOT / 'payload'):
                return None
            return previous
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def helper(self):
        directory = self.home / 'auxiliar-rpcs3'
        exe = directory / 'rpcs3.exe'
        if exe.is_file():
            # Headless RPCS3 opens log files before it creates its data folders.
            (directory / 'log').mkdir(parents=True, exist_ok=True)
            return exe
        self.notify('Baixando auxiliar RPCS3 oficial; usado somente na preparacao.')
        archive = self.home / 'rpcs3.7z'
        download(RPCS3_URL, archive, RPCS3_SHA)
        import py7zr
        staging = self.home / ('auxiliar-' + uuid.uuid4().hex)
        staging.mkdir()
        with py7zr.SevenZipFile(archive) as z:
            for name in z.getnames():
                relative = Path(name.replace('\\', '/'))
                if relative.is_absolute() or '..' in relative.parts or ':' in name:
                    raise RuntimeError('Caminho invalido no auxiliar baixado.')
        # Official RPCS3 builds use BCJ2, which py7zr cannot decompress.
        extractor = self.home / '7zr.exe'
        if not extractor.is_file():
            download('https://github.com/ip7z/7zip/releases/download/26.04/7zr.exe', extractor,
                     '256feca8e274e5da655e2a284fabafd9f554365eb164862089dacd4e8276d282')
        self.run([extractor, 'x', archive, '-o' + str(staging), '-y'], timeout=180)
        if not (staging / 'rpcs3.exe').is_file():
            raise RuntimeError('Layout do auxiliar RPCS3 nao reconhecido.')
        staging.rename(directory)
        (directory / 'log').mkdir(parents=True, exist_ok=True)
        archive.unlink()
        return exe

    def prepare(self, folder, packages, firmware=None):
        game = game_root(folder)
        previous = self.cached_build()
        if previous and not packages and not firmware:
            previous['game_root'] = str(game)
            self.save(previous)
            self.notify('Build local existente pronta para jogar; nenhuma recompilacao necessaria.')
            return
        helper = self.helper()
        self.ensure_firmware(helper, firmware)
        hdd = helper.parent / 'dev_hdd0'
        for package in packages:
            package = Path(package).resolve()
            if package.suffix.lower() != '.pkg' or not package.is_file():
                raise ValueError('Pacote PKG inexistente: ' + str(package))
            self.notify('Instalando pacote local: ' + package.name)
            rpc_log = helper.parent / 'log/RPCS3.log'
            previous_sha = digest(rpc_log) if rpc_log.is_file() else None
            try:
                self.run([helper, '--headless', '--installpkg', package], timeout=1800, cwd=helper.parent,
                         label='Instalar PKG ' + package.name)
            except StageFailure as error:
                if completed_update_install(helper.parent, package, previous_sha, error.returncode):
                    message = 'Atualizacao US 1.40 instalada; auxiliar encerrou com erro apos confirmar. O ELF sera verificado antes do uso.'
                elif completed_dlc_install(helper.parent, package, previous_sha, error.returncode):
                    message = 'Pacote DLC NPUB31321 instalado e arquivos conferidos; auxiliar encerrou com erro apos confirmar. Confira o conteudo no jogo.'
                else:
                    raise
                with (self.home / 'preparacao.log').open('a', encoding='utf-8') as log:
                    log.write(message + '\n')
                self.notify(message)
        update = hdd / 'game/BLUS31313/USRDIR'
        if not (update / 'Data/START_7.dat').is_file() or not (update / 'EBOOT.BIN').is_file():
            raise RuntimeError('A atualizacao US 1.40 (BLUS31313) nao foi encontrada. Selecione o PKG correto.')
        # Work on a copy; never write an ELF alongside the user's original EBOOT.
        inputs = self.home / 'entrada-local'
        inputs.mkdir(exist_ok=True)
        binary = inputs / 'EBOOT.BIN'
        shutil.copy2(update / 'EBOOT.BIN', binary)
        elf = binary.with_suffix('.elf')
        elf.unlink(missing_ok=True)
        self.notify('Preparando o executavel da sua copia do jogo.')
        self.run([helper, '--decrypt', binary], timeout=180, cwd=helper.parent, label='Preparar ELF US 1.40')
        if not elf.is_file() or digest(elf) != ELF_SHA:
            raise RuntimeError('Executavel diferente da versao US 1.40 suportada. Nenhuma build foi criada.')
        if previous:
            previous.update(game_root=str(game), hdd0_root=str(hdd))
            self.save(previous)
            self.notify('Conteudo importado. Build local existente pronta para jogar.')
            return
        env = build_environment()
        pending = self.home / 'build-pendente.json'
        repo = None
        if pending.is_file():
            try:
                candidate = Path(json.loads(pending.read_text(encoding='utf-8'))['workspace'])
                receipt = candidate / 'port-140-windows-generated/windows-generation.json'
                if receipt.is_file() and build_source_signature(candidate) == build_source_signature(ROOT / 'payload'):
                    repo = candidate
            except (OSError, ValueError, KeyError, TypeError):
                pass
        resume = repo is not None
        if resume:
            self.notify('Retomando a compilacao interrompida; arquivos gerados serao verificados.')
        else:
            repo = self.home / ('builds/' + uuid.uuid4().hex)
            self.notify('Preparando fontes e compilando. A primeira vez pode demorar bastante.')
            shutil.copytree(ROOT / 'payload', repo)
            pending.write_text(json.dumps(dict(workspace=str(repo))), encoding='utf-8')
            self.run([sys.executable, repo / 'tools/bootstrap_windows.py'], env, label='Preparar SDK Windows')
        command = [sys.executable, repo / 'tools/build_windows.py', '--elf', elf,
                   '--json-include', ROOT / 'vendor/include', '--jobs', '4']
        if resume:
            command.append('--resume')
        self.run(command, env, label='Gerar e compilar jogo Windows')
        self.save(dict(workspace=str(repo), game_root=str(game), hdd0_root=str(hdd)))
        pending.unlink(missing_ok=True)
        self.notify('Preparacao concluida. Clique em Jogar.')

    def save(self, config):
        self.home.mkdir(parents=True, exist_ok=True)
        target = self.home / 'build-atual.json'
        temporary = target.with_suffix('.tmp')
        temporary.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(target)

    def play(self):
        config = json.loads((self.home / 'build-atual.json').read_text(encoding='utf-8'))
        repo = Path(config['workspace'])
        game_root(config['game_root'])
        sys.path.insert(0, str(ROOT / 'windows'))
        from iniciar_build import launch_environment
        profile = json.loads((ROOT / 'windows/perfil.json').read_text())
        env = launch_environment(profile, config, os.environ)
        self.notify('Jogo em execucao.')
        log = self.home / 'logs' / ('jogo-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6] + '.log')
        self.run([repo / 'port/build-windows-140/DisgaeaD2Recomp.exe', repo / 'work/v140/EBOOT.elf'], env, cwd=repo,
                 label='Executar jogo nativo', log_path=log)
        self.notify('Jogo encerrado.')

    def install_tools(self):
        winget = shutil.which('winget')
        if not winget:
            raise RuntimeError('Instale o App Installer pela Microsoft Store para habilitar winget.')
        self.notify('Instalando ferramentas oficiais. O Windows pode pedir autorizacao de administrador.')
        commands = [
            [winget, 'install', '--id', 'Git.Git', '--exact', '--source', 'winget'],
            [winget, 'install', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '--exact', '--source', 'winget', '--override', '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --includeRecommended'],
        ]
        installed_git = shutil.which('git') or (Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/cmd/git.exe').is_file()
        if installed_git:
            commands.pop(0)
        for command in commands:
            # Show the official installer console so terms/prompts are actionable.
            result = subprocess.run(command, creationflags=subprocess.CREATE_NEW_CONSOLE)
            if result.returncode:
                raise RuntimeError('O instalador nao concluiu. Confira a janela oficial e tente novamente.')
        self.notify('Ferramentas instaladas. Reinicie o aplicativo antes de preparar o jogo.')


def gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    app = tk.Tk()
    app.title('Disgaea D2 — ' + VERSION + ' — Preparar e Jogar')
    app.geometry('800x680')
    initial_folder = ''
    try:
        initial_folder = json.loads((HOME / 'build-atual.json').read_text(encoding='utf-8')).get('game_root', '')
    except (OSError, ValueError):
        pass
    folder = tk.StringVar(value=initial_folder)
    firmware = tk.StringVar()
    firmware_text = tk.StringVar(value='Firmware: instalado no auxiliar local.' if firmware_present(HOME / 'auxiliar-rpcs3') else 'Firmware: selecione um PUP ou importe do seu RPCS3.')
    status = tk.StringVar(value='Selecione seu jogo extraido e a atualizacao US 1.40.')
    packages = []
    events = queue.Queue()
    busy = False
    backend = Backend(lambda msg: events.put(('status', msg)))
    frame = ttk.Frame(app, padding=20)
    frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='Disgaea D2 para Windows', font=('Segoe UI', 20)).pack(anchor='w')
    ttk.Label(frame, text='A primeira preparacao gera a build no seu PC. Depois, basta clicar em Jogar.', wraplength=690).pack(anchor='w', pady=12)
    ttk.Entry(frame, textvariable=folder).pack(fill='x')
    def choose_folder():
        selected = filedialog.askdirectory(title='Pasta do jogo ou PS3_GAME')
        if selected:
            folder.set(selected)
    def choose_pkgs():
        selected = filedialog.askopenfilenames(title='Atualizacao 1.40 e DLCs da sua copia', filetypes=[('Pacotes PS3', '*.pkg')])
        if selected:
            packages[:] = selected
            pkg_label.configure(text='Pacotes: ' + ', '.join(Path(p).name for p in selected))
    buttons = []
    button = ttk.Button(frame, text='Selecionar jogo extraido', command=choose_folder)
    button.pack(anchor='w', pady=6)
    buttons.append(button)
    def choose_firmware():
        selected = filedialog.askopenfilename(title='Firmware oficial PS3UPDAT.PUP', filetypes=[('Firmware PS3', '*.PUP *.pup')])
        if selected:
            firmware.set(selected)
            firmware_text.set('Firmware selecionado: ' + selected)
    def import_firmware():
        selected = filedialog.askdirectory(title='Pasta do RPCS3 ou dev_flash com firmware instalado')
        if selected:
            firmware.set(selected)
            firmware_text.set('Importar firmware de: ' + selected)
    for title, action in [('Selecionar firmware PUP', choose_firmware), ('Importar firmware do meu RPCS3', import_firmware)]:
        button = ttk.Button(frame, text=title, command=action)
        button.pack(anchor='w', pady=3)
        buttons.append(button)
    ttk.Label(frame, textvariable=firmware_text, wraplength=750).pack(anchor='w', pady=5)
    def firmware_site():
        import webbrowser
        webbrowser.open('https://www.playstation.com/pt-br/support/hardware/ps3/system-software/')
    ttk.Button(frame, text='Pagina oficial do firmware PS3', command=firmware_site).pack(anchor='w', pady=3)
    button = ttk.Button(frame, text='Selecionar atualizacao / DLCs (.pkg)', command=choose_pkgs)
    button.pack(anchor='w', pady=6)
    buttons.append(button)
    pkg_label = ttk.Label(frame, text='Nenhum PKG selecionado.', wraplength=690)
    pkg_label.pack(anchor='w', pady=6)
    def task(operation):
        nonlocal busy
        if busy:
            return
        busy = True
        for button in buttons:
            button.configure(state='disabled')
        def execute():
            try:
                operation()
            except Exception as error:
                events.put(('error', str(error)))
            finally:
                events.put(('done', ''))
        threading.Thread(target=execute, daemon=False).start()
    def install():
        if messagebox.askokcancel('Ferramentas da primeira preparacao', 'Instalar Git e ferramentas C++ da Microsoft? O download pode ocupar varios GB. Podem aparecer termos e solicitacoes do instalador oficial.'):
            task(backend.install_tools)
    def prepare():
        selected_game, selected_packages, selected_firmware = folder.get(), list(packages), firmware.get()
        def operation():
            backend.prepare(selected_game, selected_packages, selected_firmware)
            events.put(('prepared', ''))
        task(operation)
    def open_logs():
        logs = HOME / 'logs'
        logs.mkdir(parents=True, exist_ok=True)
        os.startfile(logs)
    for title, action in [('Instalar ferramentas', install), ('Preparar meu jogo', prepare), ('Jogar', lambda: task(backend.play)), ('Abrir pasta local / saves', lambda: (HOME.mkdir(parents=True, exist_ok=True), os.startfile(HOME))), ('Abrir logs do jogo', open_logs)]:
        button = ttk.Button(frame, text=title, command=action)
        button.pack(anchor='w', pady=3)
        buttons.append(button)
    ttk.Label(frame, textvariable=status, wraplength=690).pack(anchor='w', pady=12)
    def poll():
        nonlocal busy
        while not events.empty():
            kind, message = events.get()
            if kind == 'done':
                busy = False
                for button in buttons:
                    button.configure(state='normal')
            elif kind == 'prepared':
                firmware.set('')
                packages.clear()
                pkg_label.configure(text='Pacotes importados. Nao precisam ser selecionados novamente.')
                firmware_text.set('Firmware: instalado no auxiliar local.' if firmware_present(HOME / 'auxiliar-rpcs3') else 'Build existente pronta; firmware necessario somente para nova importacao.')
            else:
                status.set(message)
                if kind == 'error':
                    messagebox.showerror('Preparacao / jogo', message)
        app.after(200, poll)
    def close():
        if busy:
            messagebox.showinfo('Aguarde', 'Encerre o jogo ou aguarde a preparacao antes de fechar o aplicativo.')
        else:
            app.destroy()
    app.protocol('WM_DELETE_WINDOW', close)
    poll()
    app.mainloop()


def main():
    if os.name == 'nt':
        import ctypes
        # External compilers/helpers must use their own DLLs, not frozen Python's.
        ctypes.windll.kernel32.SetDllDirectoryW(None)
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    import multiprocessing
    multiprocessing.freeze_support()
    if len(sys.argv) > 2 and sys.argv[1] == '-c':
        code = sys.argv[2]
        # CMake probes the bundled interpreter through -c.
        worker_mode()
        exec(code, {'__name__': '__main__'})
    elif worker_mode():
        pass
    elif '--self-test' in sys.argv:
        import capstone, construct, Crypto, tabulate, tqdm, py7zr
        import argparse, dataclasses, glob, struct, tempfile
        if not (ROOT / 'payload/patches/SDK-WINDOWS.lock').is_file():
            raise RuntimeError('Payload ausente')
        lock = json.loads((ROOT / 'payload/patches/SDK-WINDOWS.lock').read_text())
        if digest(ROOT / 'payload/patches' / lock['patch']) != lock['patch_sha256']:
            raise RuntimeError('SDK patch bytes changed during packaging')
        (HOME / 'self-test-ok.txt').parent.mkdir(parents=True, exist_ok=True)
        (HOME / 'self-test-ok.txt').write_text('OK')
    elif '--toolchain-test' in sys.argv:
        env = build_environment()
        smoke = HOME / 'toolchain-test'
        smoke.mkdir(parents=True, exist_ok=True)
        (smoke / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(LauncherSmoke LANGUAGES C)\nfind_package(Python3 COMPONENTS Interpreter REQUIRED)\nadd_executable(smoke hello.c)\n')
        (smoke / 'hello.c').write_text('int main(void) { return 0; }\n')
        backend = Backend(lambda text: None)
        backend.run(['cmake', '-S', smoke, '-B', smoke / 'build', '-G', 'Ninja', '-DCMAKE_SUPPRESS_REGENERATION=ON', '-DCMAKE_C_COMPILER=clang-cl', '-DPython3_EXECUTABLE=' + sys.executable], env, timeout=60)
        backend.run(['cmake', '--build', smoke / 'build'], env, timeout=60)
        backend.run(['cmake', '--build', smoke / 'build'], env, timeout=60)
        backend.run([smoke / 'build/smoke.exe'], timeout=15)
        (HOME / 'toolchain-test-ok.txt').write_text('OK')
    elif '--auxiliary-test' in sys.argv:
        backend = Backend(lambda text: None)
        helper = backend.helper()
        # --help exercises the real log setup without game/firmware inputs.
        # An invalid --decrypt input triggers an upstream teardown assertion
        # on a fresh firmware-free RPCS3 profile, unrelated to log creation.
        result = subprocess.run([str(helper), '--help'],
                                cwd=helper.parent, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        output = result.stdout.decode('utf-8', errors='replace')
        if 'Log file open failed' in output or result.returncode != 0:
            raise RuntimeError('Headless auxiliary startup failed (exit ' + str(result.returncode) + '): ' + output[-2000:])
        if not (helper.parent / 'log/RPCS3.log').is_file():
            raise RuntimeError('Auxiliary did not create its log file')
        # Exercise the reuse path as well, without downloading again.
        if backend.helper() != helper:
            raise RuntimeError('Auxiliary reuse failed')
        (HOME / 'auxiliary-test-ok.txt').write_text('OK')
    elif len(sys.argv) > 1:
        raise ValueError('Argumentos internos nao reconhecidos: ' + repr(sys.argv[1:]))
    else:
        gui()


if __name__ == '__main__':
    try:
        main()
    except Exception:
        # Worker failures must exit, never open a hidden PyInstaller error dialog.
        import traceback
        if len(sys.argv) > 1 and sys.stderr is not None:
            # Optional CMake probes use ERROR_QUIET; respect their stderr pipe.
            traceback.print_exc(file=sys.stderr)
        else:
            HOME.mkdir(parents=True, exist_ok=True)
            with (HOME / 'preparacao.log').open('a', encoding='utf-8') as log:
                traceback.print_exc(file=log)
        sys.exit(1)
