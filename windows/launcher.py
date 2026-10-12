"""Windows first-run assistant. All game-derived output stays on this PC."""
import hashlib
import io
import json
import os
from pathlib import Path
import queue
import runpy
import shutil
import subprocess
import sys
import threading
import urllib.request
import uuid

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))
HOME = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'DisgaeaD2Recomp'
ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'
RPCS3_URL = 'https://github.com/RPCS3/rpcs3-binaries-win/releases/download/build-2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3-v0.0.43-20284-2ba2f75b_win64_msvc.7z'
RPCS3_SHA = '8914810c74508b0e53c6b6cb39d4ed334ecee99639973062d259b1481bfa1720'


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
    command = '""' + str(vcvars) + '" x64 >nul && set"'
    output = subprocess.check_output(['cmd.exe', '/d', '/s', '/c', command], text=True, errors='replace')
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

    def run(self, args, env=None, timeout=None, cwd=None):
        self.home.mkdir(parents=True, exist_ok=True)
        log = self.home / 'preparacao.log'
        self.notify('Executando: ' + Path(str(args[0])).name)
        with log.open('ab') as f:
            result = subprocess.run(list(map(str, args)), env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode:
            raise RuntimeError('A etapa falhou. Consulte ' + str(log))

    def helper(self):
        directory = self.home / 'auxiliar-rpcs3'
        exe = directory / 'rpcs3.exe'
        if exe.is_file():
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
            z.extractall(staging)
        if not (staging / 'rpcs3.exe').is_file():
            raise RuntimeError('Layout do auxiliar RPCS3 nao reconhecido.')
        staging.rename(directory)
        archive.unlink()
        return exe

    def prepare(self, folder, packages):
        game = game_root(folder)
        env = build_environment()  # Fail before touching user packages.
        helper = self.helper()
        hdd = helper.parent / 'dev_hdd0'
        for package in packages:
            package = Path(package).resolve()
            if package.suffix.lower() != '.pkg' or not package.is_file():
                raise ValueError('Pacote PKG inexistente: ' + str(package))
            self.notify('Instalando pacote local: ' + package.name)
            self.run([helper, '--headless', '--installpkg', package], timeout=1800, cwd=helper.parent)
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
        self.run([helper, '--decrypt', binary], timeout=180, cwd=helper.parent)
        if not elf.is_file() or digest(elf) != ELF_SHA:
            raise RuntimeError('Executavel diferente da versao US 1.40 suportada. Nenhuma build foi criada.')
        cache = self.home / 'build-atual.json'
        if cache.is_file():
            previous = json.loads(cache.read_text(encoding='utf-8'))
            repo = Path(previous['workspace'])
            if (repo / 'port/build-windows-140/DisgaeaD2Recomp.exe').is_file():
                previous.update(game_root=str(game), hdd0_root=str(hdd))
                self.save(previous)
                self.notify('Build local existente pronta para jogar.')
                return
        repo = self.home / ('builds/' + uuid.uuid4().hex)
        self.notify('Preparando fontes e compilando. A primeira vez pode demorar bastante.')
        shutil.copytree(ROOT / 'payload', repo)
        self.run([sys.executable, repo / 'tools/bootstrap_windows.py'], env)
        self.run([sys.executable, repo / 'tools/build_windows.py', '--elf', elf, '--json-include', ROOT / 'vendor/include', '--jobs', '4'], env)
        self.save(dict(workspace=str(repo), game_root=str(game), hdd0_root=str(hdd)))
        self.notify('Preparacao concluida. Clique em Jogar.')

    def save(self, config):
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
        self.run([repo / 'port/build-windows-140/DisgaeaD2Recomp.exe', repo / 'work/v140/EBOOT.elf'], env, cwd=repo)
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
    app.title('Disgaea D2 — Preparar e Jogar (experimental)')
    app.geometry('740x480')
    folder = tk.StringVar()
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
    ttk.Button(frame, text='Selecionar jogo extraido', command=choose_folder).pack(anchor='w', pady=6)
    ttk.Button(frame, text='Selecionar atualizacao / DLCs (.pkg)', command=choose_pkgs).pack(anchor='w')
    pkg_label = ttk.Label(frame, text='Nenhum PKG selecionado.', wraplength=690)
    pkg_label.pack(anchor='w', pady=6)
    buttons = []
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
    for title, action in [('Instalar ferramentas', install), ('Preparar meu jogo', lambda: task(lambda: backend.prepare(folder.get(), list(packages)))), ('Jogar', lambda: task(backend.play)), ('Abrir pasta local / saves', lambda: (HOME.mkdir(parents=True, exist_ok=True), os.startfile(HOME)))]:
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
        backend.run(['cmake', '-S', smoke, '-B', smoke / 'build', '-G', 'Ninja', '-DCMAKE_C_COMPILER=clang-cl', '-DPython3_EXECUTABLE=' + sys.executable], env, timeout=60)
        backend.run(['cmake', '--build', smoke / 'build'], env, timeout=60)
        backend.run([smoke / 'build/smoke.exe'], timeout=15)
        (HOME / 'toolchain-test-ok.txt').write_text('OK')
    else:
        gui()


if __name__ == '__main__':
    try:
        main()
    except Exception:
        # Worker failures must exit, never open a hidden PyInstaller error dialog.
        import traceback
        HOME.mkdir(parents=True, exist_ok=True)
        with (HOME / 'preparacao.log').open('a', encoding='utf-8') as log:
            traceback.print_exc(file=log)
        sys.exit(1)
