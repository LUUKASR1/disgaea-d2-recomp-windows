"""Windows companion logic: talks to the running game over its named pipe, reads trophy data from the player's own
TROPHY.TRP (nothing is shipped), and holds the texts of the cheats window and trophy pop-up. No GUI code here (see
companion_gui.py), so everything in this file can be tested without a display."""
import base64
import os
import re
import struct

from trophy_names import TROPHIES, TROPHY_COUNT

PIPE_NAME = r'\\.\pipe\DisgaeaD2Companion'
EXP_STEPS = (1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024)
GRADE_ORDER = 'PGSB'

TEXT = {
    'pt-BR': {
        'title': 'Disgaea D2 • Menu do jogo', 'tab_cheats': 'Cheats', 'tab_party': 'Personagens', 'tab_trophies': 'Troféus',
        'waiting': 'Aguardando o jogo...', 'close': 'Fechar (F1)', 'hp_lock': 'HP infinito', 'sp_lock': 'SP infinito',
        'one_hit': 'Dano máximo (1 golpe)', 'free_shop': 'Loja grátis', 'exp': 'Multiplicador de EXP: x{0}',
        'disable_all': 'Desligar todos os cheats', 'preset_load': 'Carregar preset {0}', 'preset_save': 'Salvar preset {0}',
        'default_preset': 'Padrão', 'on': 'LIGADO', 'off': 'DESLIGADO', 'value_prompt': 'Novo valor ({0} a {1}):',
        'refused': 'Valor recusado pelo jogo.', 'not_ready': 'O jogo ainda não está pronto para editar (abra um save).',
        'party_hint': 'Escolha um personagem', 'trophy_lock_on': 'Bloquear troféus ao usar cheats: LIGADO',
        'trophy_lock_off': 'Bloquear troféus ao usar cheats: DESLIGADO', 'trophy_test': 'Testar o aviso de troféu',
        'trophy_pick': 'Escolher TROPHY.TRP ou ISO do jogo', 'trophy_info': 'Troféus conhecidos: {0} • nomes: {1}',
        'trophy_unlocked': 'Troféu desbloqueado!', 'trophy_blocked': 'Troféu não registrado',
        'trophy_blocked_sub': 'Cheats usados com a trava ligada', 'cheats_used': 'Cheats usados nesta sessão: {0}',
        'yes': 'sim', 'no': 'não', 'hint': 'F1 abre/fecha este menu com o jogo em primeiro plano.',
        'grade': {'P': 'Platina', 'G': 'Ouro', 'S': 'Prata', 'B': 'Bronze'}, 'trophy_id': 'Troféu #{0}',
        'pick_title': 'Escolher TROPHY.TRP ou a ISO do jogo', 'pick_done': 'Ícones dos troféus carregados.',
        'pick_fail': 'Não encontrei TROPHY.TRP nesse arquivo.', 'ok': 'OK', 'cancel': 'Cancelar',
    },
    'en': {
        'title': 'Disgaea D2 • Game menu', 'tab_cheats': 'Cheats', 'tab_party': 'Characters', 'tab_trophies': 'Trophies',
        'waiting': 'Waiting for the game...', 'close': 'Close (F1)', 'hp_lock': 'Infinite HP', 'sp_lock': 'Infinite SP',
        'one_hit': 'Maximum damage (one hit)', 'free_shop': 'Free shop', 'exp': 'EXP multiplier: x{0}',
        'disable_all': 'Turn all cheats off', 'preset_load': 'Load preset {0}', 'preset_save': 'Save preset {0}',
        'default_preset': 'Default', 'on': 'ON', 'off': 'OFF', 'value_prompt': 'New value ({0} to {1}):',
        'refused': 'The game refused that value.', 'not_ready': 'The game is not ready to edit yet (open a save).',
        'party_hint': 'Pick a character', 'trophy_lock_on': 'Block trophies when cheating: ON',
        'trophy_lock_off': 'Block trophies when cheating: OFF', 'trophy_test': 'Test the trophy pop-up',
        'trophy_pick': 'Choose TROPHY.TRP or the game ISO', 'trophy_info': 'Known trophies: {0} • names: {1}',
        'trophy_unlocked': 'Trophy unlocked!', 'trophy_blocked': 'Trophy not recorded',
        'trophy_blocked_sub': 'Cheats used with the lock on', 'cheats_used': 'Cheats used this session: {0}',
        'yes': 'yes', 'no': 'no', 'hint': 'F1 opens/closes this menu while the game is in front.',
        'grade': {'P': 'Platinum', 'G': 'Gold', 'S': 'Silver', 'B': 'Bronze'}, 'trophy_id': 'Trophy #{0}',
        'pick_title': 'Choose TROPHY.TRP or the game ISO', 'pick_done': 'Trophy icons loaded.',
        'pick_fail': 'I could not find TROPHY.TRP in that file.', 'ok': 'OK', 'cancel': 'Cancel',
    },
}

# English cheat-field labels -> Portuguese (the engine sends English). Stat abbreviations stay as they are.
LABELS_PT = {
    'Bonus Gauge': 'Barra de bônus', 'Bonus Gauge progress': 'Progresso da barra de bônus', 'Cheat Shop CP': 'Loja de cheats: CP',
    'Cheat Shop EXP %': 'Loja de cheats: EXP %', 'Cheat Shop HL %': 'Loja de cheats: HL %', 'Cheat Shop Mana %': 'Loja de cheats: Mana %',
    'Cheat Shop Skill EXP %': 'Loja de cheats: EXP de habilidade %', 'Cheat Shop Weapon mastery %': 'Loja de cheats: maestria de arma %',
    'Class ID': 'ID da classe', 'Counters': 'Contra-ataques', 'Counters (base)': 'Contra-ataques (base)', 'Critical %': 'Crítico %',
    'Critical % (base)': 'Crítico % (base)', 'Item ID': 'ID do item', 'Add item ID': 'Adicionar item (ID)',
    'Item World level limit': 'Limite de nível do Mundo do Item', 'Item level': 'Nível do item', 'Jump': 'Salto',
    'Jump (base)': 'Salto (base)', 'Level': 'Nível', 'Max HP': 'HP máx.', 'Max SP': 'SP máx.', 'Movement': 'Movimento',
    'Movement (base)': 'Movimento (base)', 'Range': 'Alcance', 'Throw range (base)': 'Alcance de arremesso (base)',
    'Rarity (0 common / 1 rare / 2 legendary)': 'Raridade (0 comum / 1 raro / 2 lendário)', 'Rarity value': 'Valor de raridade',
}


def texts(language):
    return TEXT.get(language, TEXT['pt-BR'])


def field_label(english, language):
    if language != 'pt-BR':
        return english
    if english in LABELS_PT:
        return LABELS_PT[english]
    if english.endswith(' aptitude %'):
        return 'Aptidão ' + english[:-len(' aptitude %')] + ' %'
    if english.startswith('Base '):
        return english[5:] + ' base'
    return english


# ---------------------------------------------------------------------------- pipe client

class PipeError(Exception):
    pass


class PipeClient:
    """One persistent connection to the game's companion pipe. `opener` is replaceable for tests."""

    def __init__(self, name=PIPE_NAME, opener=None):
        self.name = name
        self.opener = opener or (lambda n: open(n, 'r+b', buffering=0))
        self.handle = None
        self.pid = 0

    @property
    def connected(self):
        return self.handle is not None

    def connect(self):
        if self.handle is not None:
            return True
        try:
            self.handle = self.opener(self.name)
        except OSError:
            self.handle = None
            return False
        try:
            reply = self.request('ping')
            self.pid = int(reply.split()[1]) if reply.startswith('pong') and len(reply.split()) > 1 else 0
        except (PipeError, ValueError, IndexError):
            self.close()
            return False
        return True

    def close(self):
        handle, self.handle = self.handle, None
        if handle is not None:
            try:
                handle.close()
            except OSError:
                pass

    def request(self, text):
        if self.handle is None:
            raise PipeError('not connected')
        try:
            self.handle.write(text.encode('utf-8') + b'\n')
            data = self.handle.read(256 * 1024)
        except (OSError, ValueError) as error:
            self.close()
            raise PipeError(str(error))
        if data is None:
            self.close()
            raise PipeError('closed')
        return data.decode('utf-8', errors='replace')

    def code(self, text):
        try:
            return int(self.request(text).strip() or '-2')
        except ValueError:
            return -2


def parse_state(text):
    """'key=value' lines -> dict; party entries p.N = 'name|level|class' -> state['party'] list of (name, level, class)."""
    state = {'ok': False, 'party': [], 'g': {}, 'c': {}}
    party = {}
    for line in text.splitlines():
        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        if key.startswith('g.'):
            state['g'][key[2:]] = _int(value)
        elif key.startswith('c.'):
            state['c'][key[2:]] = _int(value)
        elif key.startswith('p.'):
            parts = value.rsplit('|', 2)
            if len(parts) == 3 and key[2:].isdigit():
                party[int(key[2:])] = (parts[0], _int(parts[1]), _int(parts[2]))
        else:
            state[key] = value
    state['ok'] = state.get('ok') in ('1', True)
    for name in ('supported', 'verified', 'ready', 'hp_lock', 'sp_lock', 'one_hit', 'free_shop', 'exp', 'gen', 'unit', 'cheats_used'):
        if name in state:
            state[name] = _int(state[name])
    state['party'] = [party[i] for i in sorted(party)]
    return state


def parse_fields(text):
    """'key|label|min|max' lines -> list of (key, label, min, max)."""
    out = []
    for line in text.splitlines():
        parts = line.split('|')
        if len(parts) == 4:
            out.append((parts[0], parts[1], _int(parts[2]), _int(parts[3])))
    return out


def _int(value):
    try:
        return int(str(value).strip())
    except ValueError:
        return 0


# ---------------------------------------------------------------------------- trophies

class TrophyEntry:
    def __init__(self, name='', grade='', icon=None):
        self.name, self.grade, self.icon = name, grade, icon   # icon: PNG bytes or None


def builtin_entry(trophy_id):
    if 0 <= trophy_id < len(TROPHIES):
        return TrophyEntry(TROPHIES[trophy_id][1], TROPHIES[trophy_id][0])
    return TrophyEntry()


def png_data(png_bytes):
    """Base64 text Tk's PhotoImage(data=...) accepts."""
    return base64.b64encode(png_bytes)


class TrophyInfo:
    """Names, grades and icons from the player's TROPHY.TRP when one is available; otherwise "Trophy #N"."""

    LANGUAGE_CODES = {'pt-BR': ('17', '07'), 'en': ('01', '18')}

    def __init__(self, language='pt-BR'):
        self.language = language
        self.path = None
        self.files = {}
        self.entries = {}
        self.loaded = False

    def load(self, game_root=None, extra=None):
        self.loaded = True
        self.files.clear()
        self.entries.clear()
        self.path = find_trp(game_root) if game_root else None
        if self.path is None and extra and os.path.isfile(extra):
            self.path = extra
        if self.path is None:
            return False
        try:
            with open(self.path, 'rb') as f:
                head = f.read(0x60)
                if len(head) < 0x60 or struct.unpack('>I', head[:4])[0] != 0x00DCA24D:
                    self.path = None
                    return False
                count, size = struct.unpack('>II', head[0x10:0x18])
                if not 0 < count <= 2000 or size < 0x40:
                    self.path = None
                    return False
                table = f.read(count * size)
                for i in range(count):
                    row = table[i * size:(i + 1) * size]
                    name = row[:32].split(b'\0', 1)[0].decode('ascii', 'replace').upper()
                    offset, length = struct.unpack('>QQ', row[0x20:0x30])
                    self.files[name] = (offset, length)
                order = ['TROP_' + code + '.SFM' for code in self.LANGUAGE_CODES.get(self.language, ('01',))]
                order += ['TROP.SFM', 'TROP_01.SFM', 'TROPCONF.SFM']
                for name in order:
                    xml = self._read(f, name)
                    if xml:
                        self._parse(xml.decode('utf-8', 'replace'))
        except (OSError, struct.error):
            self.files.clear()
            self.path = None
        return bool(self.files)

    def _read(self, f, name):
        at = self.files.get(name)
        if not at or at[1] <= 0 or at[1] > 8 * 1024 * 1024:
            return None
        f.seek(at[0])
        return f.read(at[1])

    def _parse(self, xml):
        for match in re.finditer(r'<trophy\s+([^>/]*(?:"[^"]*"[^>/]*)*)>(.*?)</trophy>', xml, re.DOTALL):
            attrs, body = match.group(1), match.group(2)
            ident = re.search(r'\bid="(\d+)"', attrs)
            if not ident:
                continue
            entry = self.entries.setdefault(int(ident.group(1)), TrophyEntry())
            grade = re.search(r'\bttype="(\w)"', attrs)
            if grade and not entry.grade:
                entry.grade = grade.group(1).upper()
            name = re.search(r'<name[^>]*>(.*?)</name>', body, re.DOTALL)
            if name and not entry.name:
                entry.name = clean_xml(name.group(1))

    def get(self, trophy_id):
        entry = self.entries.get(trophy_id)
        base = builtin_entry(trophy_id)
        if entry is None:
            entry = TrophyEntry()
            self.entries[trophy_id] = entry
        if not entry.name:
            entry.name = base.name
        if not entry.grade:
            entry.grade = base.grade
        if entry.icon is None and self.path:
            try:
                with open(self.path, 'rb') as f:
                    entry.icon = self._read(f, 'TROP%03d.PNG' % trophy_id)
            except OSError:
                pass
        return entry

    def count(self):
        return max(len(self.entries), TROPHY_COUNT)

    def named(self):
        return sum(1 for entry in self.entries.values() if entry.name)


def clean_xml(raw):
    return (raw.replace('<![CDATA[', '').replace(']]>', '').replace('&amp;', '&').replace('&lt;', '<')
            .replace('&gt;', '>').replace('&quot;', '"').replace('&apos;', "'").strip())


def find_trp(root, max_depth=6):
    if not root or not os.path.isdir(str(root)):
        return None
    root = str(root)
    base_depth = root.rstrip('\\/').count(os.sep)
    for folder, dirs, files in os.walk(root):
        if folder.count(os.sep) - base_depth > max_depth:
            dirs[:] = []
            continue
        for name in files:
            if name.upper() == 'TROPHY.TRP':
                return os.path.join(folder, name)
    return None


def extract_trp_from_iso(iso_path, out_path):
    """Copies PS3_GAME/TROPDIR/<any>/TROPHY.TRP out of a plain ISO 9660 disc image. False when it is not there."""
    try:
        with open(iso_path, 'rb') as f:
            f.seek(16 * 2048)
            pvd = f.read(2048)
            if len(pvd) < 2048 or pvd[0] != 1 or pvd[1:6] != b'CD001':
                return False
            directory = (struct.unpack('<I', pvd[156 + 2:156 + 6])[0], struct.unpack('<I', pvd[156 + 10:156 + 14])[0])
            for part in ('PS3_GAME', 'TROPDIR'):
                directory = _iso_find(f, directory, part, True)
                if directory is None:
                    return False
            sub = _iso_find(f, directory, None, True)
            if sub is None:
                return False
            file = _iso_find(f, sub, 'TROPHY.TRP', False)
            if file is None or file[1] <= 0x60 or file[1] > 64 * 1024 * 1024:
                return False
            f.seek(file[0] * 2048)
            data = f.read(file[1])
            if len(data) != file[1]:
                return False
        with open(out_path, 'wb') as out:
            out.write(data)
        return True
    except (OSError, struct.error):
        return False


def _iso_find(f, directory, name, want_dir):
    f.seek(directory[0] * 2048)
    data = f.read(min(directory[1], 1 << 20))
    offset = 0
    while offset < len(data):
        length = data[offset]
        if length == 0:
            offset = (offset // 2048 + 1) * 2048
            continue
        if offset + length > len(data):
            break
        flags, name_len = data[offset + 25], data[offset + 32]
        is_dir = bool(flags & 2)
        if name_len > 0 and is_dir == want_dir and not (name_len == 1 and data[offset + 33] in (0, 1)):
            entry = data[offset + 33:offset + 33 + name_len].decode('ascii', 'replace').split(';')[0]
            if name is None or entry.upper() == name.upper():
                return (struct.unpack('<I', data[offset + 2:offset + 6])[0], struct.unpack('<I', data[offset + 10:offset + 14])[0])
        offset += length
    return None
