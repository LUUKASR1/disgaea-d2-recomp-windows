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

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))
OFFICIAL_RELEASE = (ROOT / 'vendor/official-release.json').is_file()
VERSION = '0.2.0' if OFFICIAL_RELEASE else '0.1.61-ready-wake-preview'
_standalone = '--standalone-test' in sys.argv or (ROOT / 'vendor/standalone.json').is_file()
_clean_iso = '--iso-clean-test' in sys.argv or (getattr(sys, 'frozen', False) and 'ISO-Clean' in Path(sys.executable).stem)
HOME = Path(os.environ.get('LOCALAPPDATA', Path.home())) / (
    'DisgaeaD2Recomp-Graphics-Preview' if (ROOT / 'vendor/graphics.json').is_file() else 'DisgaeaD2Recomp-Standalone' if _standalone else 'DisgaeaD2Recomp-ISO-Clean' if _clean_iso else 'DisgaeaD2Recomp')
ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'
RPCS3_URL = 'https://github.com/RPCS3/rpcs3-binaries-win/releases/download/build-2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3-v0.0.43-20284-2ba2f75b_win64_msvc.7z'
RPCS3_SHA = '8914810c74508b0e53c6b6cb39d4ed334ecee99639973062d259b1481bfa1720'
FIRMWARE_URL = 'http://dus01.ps3.update.playstation.net/update/ps3/image/us/2026_0318_a2b60b6ac1d2e49e230144345616927c/PS3UPDAT.PUP'
FIRMWARE_SHA = '158471fd834f8ea8036136b6aab43cd86c7ba73d79ca30e0af3c0fe0001cf365'


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


# Startup files observed in the supported US game. This is a presence/size
# check, not an integrity check for every asset in the campaign.
GAME_STARTUP_FILES = (
    'PS3_GAME/PARAM.SFO', 'PS3_GAME/USRDIR/EBOOT.BIN',
    *(f'PS3_GAME/USRDIR/Data/START{suffix}.dat' for suffix in ('', '_1', '_2', '_3', '_4', '_5', '_6')),
    *(f'PS3_GAME/USRDIR/Data/ANM_HI{suffix}.dat' for suffix in ('', '_1', '_2', '_3', '_4', '_5', '_6')),
    'PS3_GAME/USRDIR/Data/NisAnmShadow.txf',
    'PS3_GAME/USRDIR/Data/SOUND/SndPakBgm/SndPakBgm_0.pak',
)


def game_root(path):
    path = Path(path).resolve()
    if path.name.upper() == 'PS3_GAME':
        path = path.parent
    if not (path / 'PS3_GAME/USRDIR/EBOOT.BIN').is_file():
        raise ValueError('Selecione a pasta do jogo ou PS3_GAME contendo USRDIR/EBOOT.BIN.')
    missing = [name for name in GAME_STARTUP_FILES
               if not (path / name).is_file() or (path / name).stat().st_size == 0]
    if missing:
        raise ValueError('A pasta do jogo esta incompleta. Arquivos ausentes ou vazios:\n' +
                         '\n'.join(missing) + '\nRestaure esses arquivos da sua copia extraida antes de continuar.')
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
        with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'DisgaeaD2Launcher/' + VERSION}), timeout=90) as response, part.open('wb') as f:
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
    original = dict(os.environ)
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        raise RuntimeError('Instale as ferramentas pelo botao "Instalar ferramentas" e reinicie o aplicativo.')
    output = subprocess.check_output([str(vswhere), '-sort', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath', '-utf8'],
        encoding='utf-8-sig', timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    installs = [Path(line.strip()) for line in output.splitlines() if line.strip()]
    if not installs:
        raise RuntimeError('As ferramentas C++ do Visual Studio ainda nao estao instaladas.')
    program_files = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))
    failures = []
    for install in installs:
        vcvars = install / 'VC/Auxiliary/Build/vcvarsall.bat'
        if not vcvars.is_file():
            failures.append(str(install) + ': vcvarsall.bat ausente')
            continue
        extra = [ROOT / 'vendor/bin', ROOT / 'vendor/cmake/bin',
                 install / 'VC/Tools/Llvm/x64/bin', install / 'VC/Tools/Llvm/bin',
                 program_files / 'LLVM/bin', program_files / 'Git/cmd']
        initial_path = original.get('Path', original.get('PATH', ''))
        probe_path = os.pathsep.join(map(str, extra)) + os.pathsep + initial_path
        if not shutil.which('clang-cl', path=probe_path):
            failures.append(str(install) + ': clang-cl ausente')
            continue
        # CMD requires its own nested quote syntax around vcvarsall.
        command = 'cmd.exe /d /s /c ""' + str(vcvars) + '" x64 >nul && set"'
        try:
            output = subprocess.check_output(command, text=True, errors='replace', timeout=90,
                                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
            failures.append(str(install) + ': ambiente C++ falhou: ' + str(error))
            continue
        env = dict(original)
        for line in output.splitlines():
            name, sep, value = line.partition('=')
            if sep and name:
                env[name] = value
        env['PATH'] = os.pathsep.join(map(str, extra)) + os.pathsep + env.get('Path', env.get('PATH', ''))
        env.pop('Path', None)
        for tool in ('git', 'cmake', 'ninja', 'clang-cl'):
            if not shutil.which(tool, path=env['PATH']):
                raise RuntimeError('Ferramenta ausente: ' + tool + '. Use "Instalar ferramentas".')
        env['D2_SELECTED_VS'] = str(install)
        return env
    raise RuntimeError('Nenhuma instalacao C++ com clang-cl utilizavel foi encontrada. ' +
                       'Confira o componente LLVM no Visual Studio Installer.\n' + '\n'.join(failures))


class Backend:
    def __init__(self, notify, home=HOME):
        self.home = Path(home)
        self.notify = notify
        self.capture_enabled = False
        self.performance_enabled = False
        self.texture_reuse_enabled = False
        self.vertex_context_enabled = False
        self.last_performance_report = None
        self.capture_goal = "scenery"
        self.capture_session = None
        self.last_capture_report = None

    def request_frame_capture(self):
        if self.capture_session is None:
            raise RuntimeError('Habilite a captura antes de abrir o jogo.')
        self.capture_session.request()

    def frame_capture_status(self):
        return self.capture_session.status() if self.capture_session else 'Nenhuma sessão de captura ativa.'

    @property
    def standalone(self):
        return self.home.name in ('DisgaeaD2Recomp-Standalone', 'DisgaeaD2Recomp-Graphics-Preview')

    def run(self, args, env=None, timeout=None, cwd=None, label=None, log_path=None, on_started=None):
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
                if on_started is None:
                    result = subprocess.run(args, env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                else:
                    with subprocess.Popen(args, env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)) as result:
                        on_started(result)
                        result.wait(timeout=timeout)
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
        if not firmware and not firmware_present(directory):
            source = self.home / 'downloads/PS3UPDAT-4.93.PUP'
            if not source.is_file() or digest(source) != FIRMWARE_SHA:
                self.notify('Baixando firmware PS3 4.93 da Sony. Somente na primeira preparação.')
                download(FIRMWARE_URL, source, FIRMWARE_SHA)
            firmware = str(source)
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
        if self.standalone: raise RuntimeError('O perfil standalone não utiliza RPCS3.')
        directory = self.home / 'auxiliar-rpcs3'
        exe = directory / 'rpcs3.exe'
        if exe.is_file():
            # Headless RPCS3 opens log files before it creates its data folders.
            (directory / 'log').mkdir(parents=True, exist_ok=True)
            self.helper_support(directory)
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
        self.helper_support(directory)
        return exe

    def helper_support(self, directory):
        from runtime_dependencies import ensure_runtimes, VC_FILES
        # Keep user data and firmware in place; preserve misplaced old DLLs.
        names = set(VC_FILES) | {'vulkan-1.dll', 'concrt140.dll', 'vccorlib140.dll',
                                 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vcruntime140_threads.dll'}
        misplaced = [directory / name for name in names if (directory / name).is_file()]
        if misplaced:
            backup = directory / ('runtimes-anteriores-' + uuid.uuid4().hex)
            backup.mkdir()
            for file in misplaced:
                file.rename(backup / file.name)
            self.notify('Bibliotecas locais anteriores preservadas; usando a instalação oficial do Windows.')
        ensure_runtimes(self.home, download, self.notify)

    def prepare(self, folder, packages, firmware=None, iso_path=None):
        if self.standalone and (not iso_path or firmware):
            raise ValueError('Standalone requer ISO e PKGs; firmware não é necessário.')
        game = self.prepare_iso_source(iso_path) if iso_path else self.check_game_files(folder)
        packages = [Path(package).resolve() for package in packages]
        for package in packages:
            if package.suffix.lower() != '.pkg' or not package.is_file() or not package.stat().st_size:
                raise ValueError('Pacote PKG inexistente ou vazio: ' + str(package))
        previous = self.cached_build()
        if iso_path and previous and not previous.get("native_iso"):
            previous = None
        if previous and not packages and not firmware:
            previous.update(game_root=str(game), iso_path=str(iso_path) if iso_path else None)
            self.save(previous)
            (self.home / 'preparacao-inputs.json').unlink(missing_ok=True)
            self.notify('Build local existente pronta para jogar; nenhuma recompilacao necessaria.')
            return
        self.home.mkdir(parents=True, exist_ok=True)
        inputs_record = self.home / 'preparacao-inputs.json'
        temporary = inputs_record.with_suffix('.tmp')
        temporary.write_text(json.dumps(dict(game_root=str(game), packages=list(map(str, packages)),
                                            firmware=str(firmware) if firmware else '', iso_path=str(iso_path) if iso_path else None), ensure_ascii=False), encoding='utf-8')
        temporary.replace(inputs_record)
        if self.standalone:
            hdd = self.home / 'hdd0'
            for package in packages:
                self.run([sys.executable, ROOT / 'payload/tools/import_owned_pkg.py',
                          '--pkg', package, '--hdd', hdd], label='Importar PKG sem auxiliar: ' + package.name)
        else:
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
        self.notify('Preparando o executavel da sua copia do jogo.')
        if self.standalone:
            self.run([sys.executable, ROOT / 'payload/tools/prepare_owned_self.py', '--self', binary,
                      '--elf', elf], timeout=180, label='Preparar ELF 1.40 sem firmware ou auxiliar')
        else:
            elf.unlink(missing_ok=True)
            self.run([helper, '--decrypt', binary], timeout=180, cwd=helper.parent, label='Preparar ELF US 1.40')
        if not elf.is_file() or digest(elf) != ELF_SHA:
            raise RuntimeError('Executavel diferente da versao US 1.40 suportada. Nenhuma build foi criada.')
        if previous:
            previous.update(game_root=str(game), hdd0_root=str(hdd), iso_path=str(iso_path) if iso_path else None)
            self.save(previous)
            inputs_record.unlink(missing_ok=True)
            self.notify('Conteudo importado. Build local existente pronta para jogar.')
            return
        env = self.compiler_environment()
        pending = self.home / 'build-pendente.json'
        repo = None
        if pending.is_file():
            try:
                pending_config = json.loads(pending.read_text(encoding='utf-8'))
                candidate = Path(pending_config['workspace'])
                receipt = candidate / 'port-140-windows-generated/windows-generation.json'
                if (pending_config.get('toolchain') == env.get('D2_TOOLCHAIN_ID') and
                        receipt.is_file() and build_source_signature(candidate) == build_source_signature(ROOT / 'payload')):
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
            pending.write_text(json.dumps(dict(workspace=str(repo), toolchain=env.get('D2_TOOLCHAIN_ID'))), encoding='utf-8')
            self.run([sys.executable, repo / 'tools/bootstrap_windows.py'], env, label='Preparar SDK Windows')
        command = [sys.executable, repo / 'tools/build_windows.py', '--elf', elf,
                   '--json-include', ROOT / 'vendor/include', '--jobs', '4']
        if iso_path:
            command.append('--iso-vfs')
        if resume:
            command.append('--resume')
        self.run(command, env, label='Gerar e compilar jogo Windows')
        self.save(dict(workspace=str(repo), game_root=str(game), hdd0_root=str(hdd),
                       iso_path=str(iso_path) if iso_path else None, native_iso=bool(iso_path), standalone=self.standalone))
        pending.unlink(missing_ok=True)
        inputs_record.unlink(missing_ok=True)
        self.notify('Preparação concluída.')

    def prepare_and_play(self, folder, packages=(), firmware=None, on_prepared=None):
        self.prepare(folder, packages, firmware)
        if on_prepared:
            on_prepared()
        self.play()

    def open_clean_iso_test(self):
        command = [sys.executable]
        if not getattr(sys, 'frozen', False): command.append(str(Path(__file__).resolve()))
        subprocess.Popen(command + ['--iso-clean-test'])

    def prepare_iso_source(self, image):
        image = Path(image).resolve()
        if image.suffix.lower() != '.iso' or not image.is_file():
            raise ValueError('Selecione uma ISO existente USA BLUS31313.')
        env = self.compiler_environment()
        result = self.home / ('iso-reader-' + uuid.uuid4().hex + '.txt')
        try:
            self.run([sys.executable, ROOT / 'payload/tools/build_iso_reader.py',
                      '--source', ROOT / 'payload/port/vfs', '--output', self.home / 'iso-readers',
                      '--result', result], env, label='Preparar leitor ISO nativo')
            reader = Path(result.read_text(encoding='utf-8'))
        finally:
            result.unlink(missing_ok=True)
        stat = image.stat()
        key = hashlib.sha256((str(image) + str(stat.st_size) + str(stat.st_mtime_ns)).encode()).hexdigest()[:24]
        root = self.home / 'iso-metadata' / key
        self.run([reader, '--metadata-root', root, image], env, label='Validar ISO e ler metadados')
        if not (root / 'PS3_GAME/PARAM.SFO').is_file():
            raise RuntimeError('Leitor terminou sem metadados do jogo.')
        return root

    def prepare_iso_and_play(self, image, packages=(), firmware=None, on_prepared=None):
        image = Path(image).resolve()
        self.prepare(None, packages, firmware, iso_path=image)
        if on_prepared: on_prepared()
        self.play()

    def check_game_files(self, folder):
        try:
            return game_root(folder)
        except (ValueError, OSError) as error:
            self.home.mkdir(parents=True, exist_ok=True)
            with (self.home / 'preparacao.log').open('a', encoding='utf-8') as log:
                log.write(datetime.now().isoformat(timespec='seconds') + ' launcher=' + VERSION + ' etapa=Verificar arquivos do jogo\n')
                log.write('falha_arquivos=' + str(error) + '\n')
            raise

    def compiler_environment(self):
        self.home.mkdir(parents=True, exist_ok=True)
        log_path = self.home / 'preparacao.log'
        with log_path.open('a', encoding='utf-8') as log:
            log.write(datetime.now().isoformat(timespec='seconds') + ' launcher=' + VERSION + ' etapa=Detectar ferramentas C++\n')
            try:
                from portable_tools import portable_environment
                env = portable_environment(self.home, ROOT, download, self.notify)
            except Exception as error:
                log.write('falha_deteccao=' + type(error).__name__ + ': ' + str(error) + '\n')
                raise
            log.write('toolchain=' + env.get('D2_TOOLCHAIN_ID', '') + '\n')
            for tool in ('git', 'cmake', 'ninja'):
                log.write(tool + '=' + str(shutil.which(tool, path=env['PATH'])) + '\n')
            return env

    def save(self, config):
        self.home.mkdir(parents=True, exist_ok=True)
        target = self.home / 'build-atual.json'
        temporary = target.with_suffix('.tmp')
        temporary.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(target)

    def play_iso(self, image):
        image = Path(image).resolve()
        if image.suffix.lower() != '.iso' or not image.is_file():
            raise ValueError('Selecione uma ISO existente do jogo USA BLUS31313.')
        try:
            config = json.loads((self.home / 'build-atual.json').read_text(encoding='utf-8'))
        except (OSError, ValueError):
            raise RuntimeError('Prepare o jogo USA 1.40 primeiro no launcher atual. Este teste reutiliza a preparacao existente.')
        if config.get('native_iso'):
            metadata = self.prepare_iso_source(image)
            self.play(iso_path=image, iso_executable=Path(config['workspace']) / 'port/build-windows-140/DisgaeaD2Recomp.exe', iso_root=metadata)
            return
        source = ROOT / 'payload'
        identity = build_source_signature(source) + digest(source / 'tools/build_iso_preview.py') + str(Path(config['workspace']).resolve())
        key = hashlib.sha256(identity.encode()).hexdigest()[:24]
        preview = self.home / 'iso-builds' / key
        env = self.compiler_environment()
        self.notify('Preparando o modo ISO. Na primeira vez, aguarde a compilação automática.')
        self.run([sys.executable, source / 'tools/build_iso_preview.py', '--source', source,
                  '--workspace', config['workspace'], '--preview', preview,
                  '--json-include', ROOT / 'vendor/include'], env, label='Preparar runtime ISO experimental')
        metadata = self.prepare_iso_source(image)
        self.play(iso_path=image, iso_executable=preview / 'build/DisgaeaD2Recomp.exe', iso_root=metadata)

    def adopt_standalone_for_graphics(self):
        if self.home.name != 'DisgaeaD2Recomp-Graphics-Preview' or (self.home / 'build-atual.json').exists(): return
        previous = self.home.parent / 'DisgaeaD2Recomp-Standalone/build-atual.json'
        if not previous.is_file(): return
        config = json.loads(previous.read_text(encoding='utf-8'))
        repo = Path(config['workspace'])
        if (config.get('native_iso') and config.get('standalone') and
                digest(repo / 'work/v140/EBOOT.elf') == ELF_SHA):
            self.save(config)

    def graphics_runtime(self, config):
        source = ROOT / 'payload'
        identity = build_source_signature(source) + digest(source / 'tools/build_iso_preview.py') + str(Path(config['workspace']).resolve())
        key = hashlib.sha256(identity.encode()).hexdigest()[:24]
        output = self.home / 'graphics-builds' / key
        env = self.compiler_environment()
        self.run([sys.executable, source / 'tools/build_iso_preview.py', '--source', source,
                  '--workspace', config['workspace'], '--preview', output,
                  '--json-include', ROOT / 'vendor/include'], env, label='Preparar runtime gráfico separado')
        return output / 'build/DisgaeaD2Recomp.exe'

    def play(self, iso_path=None, iso_executable=None, iso_root=None):
        config = json.loads((self.home / 'build-atual.json').read_text(encoding='utf-8'))
        repo = Path(config['workspace'])
        if iso_path is None and config.get('iso_path'):
            iso_path = Path(config['iso_path'])
            iso_executable = repo / 'port/build-windows-140/DisgaeaD2Recomp.exe'
        if self.home.name == 'DisgaeaD2Recomp-Graphics-Preview':
            iso_executable = self.graphics_runtime(config)
        if iso_path is None:
            self.check_game_files(config['game_root'])
        elif not Path(iso_path).is_file() or not iso_executable or not Path(iso_executable).is_file():
            raise RuntimeError('ISO ou executavel experimental ausente.')
        sys.path.insert(0, str(ROOT / 'windows'))
        from iniciar_build import launch_environment
        profile = json.loads((ROOT / 'windows/perfil.json').read_text())
        from display_settings import load_settings, display_environment, configure_game_window
        settings = load_settings(self.home)
        env = display_environment(launch_environment(profile, config, os.environ), settings)
        from frame_capture import CaptureSession, clean_capture_environment
        env = clean_capture_environment(env)
        from performance_report import PerformanceSession, clean_environment
        env = clean_environment(env)
        from texture_reuse_settings import texture_reuse_environment, vertex_context_environment, ready_wake_environment
        reuse_enabled = bool(self.texture_reuse_enabled and not OFFICIAL_RELEASE)
        env = texture_reuse_environment(env, reuse_enabled, OFFICIAL_RELEASE)
        vertex_enabled = bool(self.vertex_context_enabled and not OFFICIAL_RELEASE)
        env = vertex_context_environment(env, vertex_enabled, OFFICIAL_RELEASE)
        ready_wake_enabled = not OFFICIAL_RELEASE
        env = ready_wake_environment(env, ready_wake_enabled, OFFICIAL_RELEASE)
        self.last_performance_report = None
        performance = None
        if self.performance_enabled and not OFFICIAL_RELEASE:
            if self.capture_enabled:
                raise RuntimeError('Desative as capturas de imagens antes de medir desempenho.')
            performance = PerformanceSession(self.home, VERSION,
                dict(settings, texture_reuse_experiment=reuse_enabled,
                     vertex_context_experiment=vertex_enabled,
                     ready_wake_experiment=ready_wake_enabled))
            env = performance.environment(env)
            self.notify('Medição leve: F6 Castle, F7 batalha, F9 Mothman, F10 concluir. Feche o jogo para receber o ZIP.')
        session = None
        self.last_capture_report = None
        if not OFFICIAL_RELEASE and self.capture_enabled and self.home.name == 'DisgaeaD2Recomp-Graphics-Preview':
            session = CaptureSession(self.home, VERSION, settings, capture_goal=self.capture_goal)
            env = session.environment(env)
        self.capture_session = session
        env.pop('D2_GAME_ISO', None)
        if iso_root is not None:
            env['PS3_VFS_ROOT'] = str(iso_root)
        self.notify('Jogo em execução.')
        stop = threading.Event()
        workers = []
        def on_started(process):
            if session: session.started()
            def configure():
                try:
                    configure_game_window(process, settings, self.notify, stop)
                except Exception as error:
                    message = 'Não foi possível ajustar a janela: ' + str(error)
                    self.notify(message)
                    with (self.home / 'preparacao.log').open('a', encoding='utf-8') as output:
                        output.write(datetime.now().isoformat(timespec='seconds') + ' ' + message + '\n')
            thread = threading.Thread(target=configure, daemon=True)
            workers.append(thread)
            thread.start()
        log = self.home / 'logs' / (('jogo-ISO-' if iso_path else 'jogo-') + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6] + '.log')
        try:
            command = [iso_executable or repo / 'port/build-windows-140/DisgaeaD2Recomp.exe', repo / 'work/v140/EBOOT.elf']
            if iso_path: command += ['--iso', Path(iso_path)]
            self.run(command, env, cwd=repo,
                     label='Executar jogo nativo', log_path=log, on_started=on_started)
        finally:
            stop.set()
            for thread in workers:
                thread.join(timeout=1)
            if session:
                try:
                    self.last_capture_report = session.finish(log)
                except Exception as error:
                    self.notify('Não foi possível gerar o ZIP. As capturas permanecem em ' + str(session.directory) + ': ' + str(error))
            if performance:
                try:
                    self.last_performance_report = performance.finish()
                    self.notify('Relatório de desempenho: ' + str(self.last_performance_report))
                except Exception as error:
                    self.notify('Não foi possível resumir o desempenho. Dados preservados em ' + str(performance.directory) + ': ' + str(error))
        self.notify('Jogo encerrado.' + (' Desempenho: ' + str(self.last_performance_report) if self.last_performance_report else '') + (' Capturas: ' + str(self.last_capture_report) if self.last_capture_report else ''))

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
    from launcher_gui import run_gui
    Backend(lambda text: None).adopt_standalone_for_graphics()
    run_gui(Backend, HOME, VERSION, firmware_present, official=OFFICIAL_RELEASE)


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
        from launcher_i18n import Localizer
        if Localizer('en')('Salvar configurações') != 'Save settings':
            raise RuntimeError('Bundled English catalog is unavailable')
        if not (ROOT / 'payload/patches/SDK-WINDOWS.lock').is_file():
            raise RuntimeError('Payload ausente')
        lock = json.loads((ROOT / 'payload/patches/SDK-WINDOWS.lock').read_text())
        if digest(ROOT / 'payload/patches' / lock['patch']) != lock['patch_sha256']:
            raise RuntimeError('SDK patch bytes changed during packaging')
        for asset in ('logo-winport.png', 'launcher-icon.png', 'launcher.ico'):
            if not (ROOT / 'windows/assets' / asset).is_file():
                raise RuntimeError('Arte do launcher ausente: ' + asset)
        if OFFICIAL_RELEASE:
            if VERSION != '0.2.0' or not _standalone or HOME.name != 'DisgaeaD2Recomp-Graphics-Preview':
                raise RuntimeError('Official release identity/profile mismatch')
        (HOME / 'self-test-ok.txt').parent.mkdir(parents=True, exist_ok=True)
        (HOME / 'self-test-ok.txt').write_text('OK')
    elif '--native-preparation-test' in sys.argv:
        import unittest
        sys.path.insert(0, str(ROOT / 'payload/tools'))
        sys.path.insert(0, str(ROOT / 'payload/windows'))
        import test_native_preparation
        HOME.mkdir(parents=True, exist_ok=True)
        with (HOME / 'native-preparation-test.log').open('w', encoding='utf-8') as output:
            result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(test_native_preparation))
        if not result.wasSuccessful(): raise RuntimeError('Embedded PKG/SELF regressions failed')
        if HOME.name not in ('DisgaeaD2Recomp-Standalone', 'DisgaeaD2Recomp-Graphics-Preview'): raise RuntimeError('Standalone profile isolation failed')
    elif '--toolchain-test' in sys.argv:
        backend = Backend(lambda text: None)
        env = backend.compiler_environment()
        smoke = HOME / 'toolchain-test'
        smoke.mkdir(parents=True, exist_ok=True)
        (smoke / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(LauncherSmoke LANGUAGES C CXX)\nfind_package(Python3 COMPONENTS Interpreter REQUIRED)\nadd_executable(smoke hello.cpp)\ntarget_link_libraries(smoke PRIVATE d3dcompiler xinput9_1_0 ole32)\n')
        # Exercise both legacy SDK TLS spelling and C++ TLS, without a GPU.
        (smoke / 'hello.cpp').write_text(r'''#include <windows.h>
#include <d3dcompiler.h>
#include <xinput.h>
#include <thread>
#include <string>
#include <cwchar>
#include <stdexcept>
#include <smmintrin.h>
__declspec(thread) int sdk_tls = 0;
thread_local int cpp_tls = 0;
int main(int argc, char** argv) {
    if (argc == 2) {
        auto length = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        std::wstring directory(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, directory.data(), length);
        directory.pop_back(); SetDllDirectoryW(directory.c_str());
        const wchar_t* names[] = {L"msvcp140.dll", L"msvcp140_1.dll", L"msvcp140_2.dll",
            L"vcruntime140.dll", L"vcruntime140_1.dll", L"concrt140.dll", L"vulkan-1.dll"};
        for (auto name : names) {
            auto library = LoadLibraryW((directory + L"/" + name).c_str());
            if (!library) return 6;
            wchar_t loaded[MAX_PATH]; GetModuleFileNameW(library, loaded, MAX_PATH);
            std::wstring actual(loaded);
            if (_wcsicmp(actual.substr(0, actual.find_last_of(L"/\\\\")).c_str(), directory.c_str()) != 0) return 7;
        }
        return 0;
    }
    sdk_tls = 7; cpp_tls = 9;
    bool ok = false;
    std::thread child([&] { ok = sdk_tls == 0 && cpp_tls == 0;
        sdk_tls = 1; cpp_tls = 2; });
    child.join();
    if (!ok || sdk_tls != 7 || cpp_tls != 9) return 1;
    ULONG_PTR low, high; GetCurrentThreadStackLimits(&low, &high);
    if (high <= low) return 2;
    if (_mm_extract_epi32(_mm_set1_epi32(42), 1) != 42) return 3;
    try { throw std::runtime_error("probe"); }
    catch (const std::exception& e) { if (std::string(e.what()) != "probe") return 4; }
    XINPUT_STATE pad{}; XInputGetState(0, &pad);
    const char shader[] = "float4 main():SV_Target{return float4(1,0,0,1);}";
    ID3DBlob *code = nullptr, *errors = nullptr;
    auto result = D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &code, &errors);
    if (errors) errors->Release();
    if (FAILED(result) || !code) return 5;
    code->Release(); return 0;
}
''')
        backend.run(['cmake', '-S', smoke, '-B', smoke / 'build', '-G', 'Ninja',
                     '-DCMAKE_SUPPRESS_REGENERATION=ON', '-DCMAKE_C_COMPILER=' + env['D2_PORTABLE_COMPILER'],
                     '-DCMAKE_CXX_COMPILER=' + env['D2_PORTABLE_CXX'],
                     '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions -msse4.1',
                     '-DCMAKE_EXE_LINKER_FLAGS=-static', '-DPython3_EXECUTABLE=' + sys.executable], env, timeout=120)
        backend.run(['cmake', '--build', smoke / 'build'], env, timeout=60)
        backend.run(['cmake', '--build', smoke / 'build'], env, timeout=60)
        backend.run([smoke / 'build/smoke.exe'], timeout=15)
        sdk_probe = HOME / 'portable-sdk-test'
        if not sdk_probe.exists():
            shutil.copytree(ROOT / 'payload', sdk_probe)
        if not (sdk_probe / 'ps3recomp').exists():
            backend.run([sys.executable, sdk_probe / 'tools/bootstrap_windows.py'], env,
                        timeout=240, label='Testar SDK com Git portátil')
        backend.run(['cmake', '-S', sdk_probe / 'ps3recomp', '-B', sdk_probe / 'runtime-build', '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
                     '-DCMAKE_C_COMPILER=' + env['D2_PORTABLE_COMPILER'],
                     '-DCMAKE_CXX_COMPILER=' + env['D2_PORTABLE_CXX'],
                     '-DCMAKE_RC_COMPILER=' + env['D2_PORTABLE_RC'],
                     '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
                     '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions'], env, timeout=120)
        backend.run(['cmake', '--build', sdk_probe / 'runtime-build', '--parallel', '4'], env,
                    timeout=360, label='Compilar runtime com ferramentas portáteis')
        (HOME / 'toolchain-test-ok.txt').write_text('OK')
    elif '--auxiliary-test' in sys.argv:
        backend = Backend(lambda text: None)
        helper = backend.helper()
        probe = HOME / 'toolchain-test/build/smoke.exe'
        if not probe.is_file():
            raise RuntimeError('Portable runtime loading probe missing')
        from runtime_dependencies import system_directory
        backend.run([probe, system_directory()], timeout=30, label='Verificar runtimes oficiais do Windows')
        # --help exercises the real log setup without game/firmware inputs.
        # An invalid --decrypt input triggers an upstream teardown assertion
        # on a fresh firmware-free RPCS3 profile, unrelated to log creation.
        try:
            result = subprocess.run([str(helper), '--help'],
                                    cwd=helper.parent, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired as error:
            log = helper.parent / 'log/RPCS3.log'
            detail = log.read_text(encoding='utf-8', errors='replace')[-4000:] if log.is_file() else 'No RPCS3 log created.'
            raise RuntimeError('Auxiliary startup timed out: ' + (error.stdout or b'').decode('utf-8', errors='replace') + '\n' + detail) from error
        output = result.stdout.decode('utf-8', errors='replace')
        if 'Log file open failed' in output or result.returncode != 0:
            raise RuntimeError('Headless auxiliary startup failed (exit ' + str(result.returncode) + '): ' + output[-2000:])
        if not (helper.parent / 'log/RPCS3.log').is_file():
            raise RuntimeError('Auxiliary did not create its log file')
        # Exercise the reuse path as well, without downloading again.
        if backend.helper() != helper:
            raise RuntimeError('Auxiliary reuse failed')
        (HOME / 'auxiliary-test-ok.txt').write_text('OK')
    elif '--runtime-setup' in sys.argv or '--runtime-test' in sys.argv:
        from runtime_dependencies import ensure_runtimes
        backend = Backend(lambda text: None)
        ensure_runtimes(HOME, download, backend.notify, force='--runtime-test' in sys.argv)
        HOME.mkdir(parents=True, exist_ok=True)
        (HOME / 'runtime-test-ok.txt').write_text('OK')
    elif len(sys.argv) == 3 and sys.argv[1] == '--install-runtimes':
        from runtime_dependencies import install_manifest
        if not ctypes.windll.shell32.IsUserAnAdmin():
            raise RuntimeError('O Windows precisa autorizar a instalação dos runtimes.')
        install_manifest(sys.argv[2])
    elif '--firmware-test' in sys.argv:
        backend = Backend(lambda text: None)
        helper = backend.helper()
        backend.ensure_firmware(helper, None)
        if not firmware_present(helper.parent):
            raise RuntimeError('Automatic firmware installation incomplete')
        backend.ensure_firmware(helper, None)
        (HOME / 'firmware-test-ok.txt').write_text('OK')
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
