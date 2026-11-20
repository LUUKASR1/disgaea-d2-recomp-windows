import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import companion
from trophy_names import TROPHIES


class FakeHandle:
    def __init__(self, replies):
        self.replies, self.sent, self.closed = list(replies), [], False

    def write(self, data):
        self.sent.append(data.decode())

    def read(self, size):
        if not self.replies:
            raise OSError('broken pipe')
        return self.replies.pop(0).encode()

    def close(self):
        self.closed = True


def make_trp(path, files):
    names = list(files)
    size, header = 0x40, 0x60
    offset = header + size * len(names)
    table, blobs = b'', b''
    for name in names:
        data = files[name]
        row = name.encode().ljust(0x20, b'\0') + struct.pack('>QQ', offset + len(blobs), len(data)) + b'\0' * 0x10
        table += row
        blobs += data
    head = struct.pack('>I', 0x00DCA24D) + b'\0' * 12 + struct.pack('>II', len(names), size) + b'\0' * (header - 0x18)
    Path(path).write_bytes(head + table + blobs)


class CompanionTests(unittest.TestCase):
    def test_builtin_trophy_list(self):
        self.assertEqual(len(TROPHIES), 74)
        self.assertEqual(TROPHIES[0][0], 'P')
        self.assertEqual(companion.builtin_entry(1).name, 'Ultimate Destroyer')
        self.assertEqual(companion.builtin_entry(999).name, '')

    def test_parse_state_and_fields(self):
        state = companion.parse_state('ok=1\nready=1\nstatus=Ready\nhp_lock=1\nexp=8\ngen=5\nunit=2\ng.hl=100\nc.level=30\np.0=Laharl|30|12\np.1=Etna|25|3\n')
        self.assertTrue(state['ok'] and state['ready'] == 1 and state['exp'] == 8 and state['unit'] == 2)
        self.assertEqual(state['g'], {'hl': 100})
        self.assertEqual(state['c'], {'level': 30})
        self.assertEqual(state['party'], [('Laharl', 30, 12), ('Etna', 25, 3)])
        self.assertFalse(companion.parse_state('ok=0\nstatus=waiting\n')['ok'])
        self.assertEqual(companion.parse_fields('hl|HL|0|999\nbad\nexp|EXP|0|5\n'), [('hl', 'HL', 0, 999), ('exp', 'EXP', 0, 5)])

    def test_labels(self):
        self.assertEqual(companion.field_label('Max HP', 'pt-BR'), 'HP máx.')
        self.assertEqual(companion.field_label('ATK aptitude %', 'pt-BR'), 'Aptidão ATK %')
        self.assertEqual(companion.field_label('Base HP', 'pt-BR'), 'HP base')
        self.assertEqual(companion.field_label('Max HP', 'en'), 'Max HP')

    def test_pipe_client(self):
        handle = FakeHandle(['pong 4242', '0', '-1'])
        client = companion.PipeClient(opener=lambda name: handle)
        self.assertTrue(client.connect() and client.pid == 4242)
        self.assertEqual(client.code('toggle hp_lock 1'), 0)
        self.assertEqual(client.code('general bad 1'), -1)
        self.assertEqual(handle.sent[0], 'ping\n')
        with self.assertRaises(companion.PipeError):
            client.request('state')
        self.assertFalse(client.connected)
        self.assertTrue(handle.closed)

    def test_pipe_client_without_game(self):
        def refuse(name):
            raise FileNotFoundError(name)
        client = companion.PipeClient(opener=refuse)
        self.assertFalse(client.connect())
        self.assertFalse(client.connected)

    def test_trp_names_and_icon(self):
        xml = ('<trophyconf><trophy id="00" ttype="P"><name>Platina PT</name></trophy>'
               '<trophy id="01" ttype="G" hidden="no"><name><![CDATA[Ouro &amp; Co]]></name></trophy></trophyconf>').encode()
        with tempfile.TemporaryDirectory() as folder:
            trp = os.path.join(folder, 'TROPHY.TRP')
            make_trp(trp, {'TROP_17.SFM': xml, 'TROP001.PNG': b'\x89PNGdata'})
            info = companion.TrophyInfo('pt-BR')
            self.assertTrue(info.load(folder))
            first = info.get(0)
            self.assertEqual((first.name, first.grade), ('Platina PT', 'P'))
            self.assertEqual(info.get(1).name, 'Ouro & Co')
            self.assertEqual(info.get(1).icon, b'\x89PNGdata')
            self.assertEqual(info.get(2).name, 'Item Hoarder')   # not in the TRP: built-in fallback
            self.assertEqual(info.get(2).grade, 'G')

    def test_trp_missing_falls_back(self):
        info = companion.TrophyInfo('en')
        self.assertFalse(info.load('Z:/definitely/not/here'))
        self.assertEqual(info.get(1).name, 'Ultimate Destroyer')
        self.assertEqual(info.count(), 74)

    def test_extract_from_iso(self):
        def record(name, extent, size, directory):
            raw = name.encode()
            length = 33 + len(raw)
            length += length % 2
            row = bytearray(length)
            row[0] = length
            struct.pack_into('<I', row, 2, extent)
            struct.pack_into('<I', row, 10, size)
            row[25] = 2 if directory else 0
            row[32] = len(raw)
            row[33:33 + len(raw)] = raw
            return bytes(row)
        trp = bytearray(b'\0' * 0x100)
        sector = 2048
        iso = bytearray(sector * 24)
        pvd = bytearray(sector)
        pvd[0], pvd[1:6] = 1, b'CD001'
        root = record('\0', 18, sector, True)
        pvd[156:156 + len(root)] = root
        iso[16 * sector:17 * sector] = pvd
        layout = {18: [record('PS3_GAME', 19, sector, True)], 19: [record('TROPDIR', 20, sector, True)],
                  20: [record('NPWR00000_00', 21, sector, True)], 21: [record('TROPHY.TRP;1', 22, len(trp), False)]}
        for extent, rows in layout.items():
            data = b''.join(rows)
            iso[extent * sector:extent * sector + len(data)] = data
        iso[22 * sector:22 * sector + len(trp)] = trp
        with tempfile.TemporaryDirectory() as folder:
            image, out = os.path.join(folder, 'game.iso'), os.path.join(folder, 'TROPHY.TRP')
            Path(image).write_bytes(bytes(iso))
            self.assertTrue(companion.extract_trp_from_iso(image, out))
            self.assertEqual(Path(out).stat().st_size, len(trp))
            Path(image).write_bytes(b'not an iso' * 5000)
            self.assertFalse(companion.extract_trp_from_iso(image, out))


if __name__ == '__main__':
    unittest.main()
