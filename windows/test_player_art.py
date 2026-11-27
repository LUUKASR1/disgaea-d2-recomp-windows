import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from player_art import find_title_art


def png(width, height):
    raw = b''.join(b'\x00' + b'\x00\x00\x00' * width for _ in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def iso_with(name, data):
    """A minimal ISO 9660 image: root -> PS3_GAME -> name."""
    def record(lba, size, ident, is_dir):
        ident = ident.encode()
        length = 33 + len(ident) + (len(ident) + 1) % 2
        rec = bytearray(length)
        rec[0] = length
        rec[2:10] = struct.pack('<I', lba) + struct.pack('>I', lba)
        rec[10:18] = struct.pack('<I', size) + struct.pack('>I', size)
        rec[25] = 2 if is_dir else 0
        rec[32] = len(ident)
        rec[33:33 + len(ident)] = ident
        return bytes(rec)
    image = bytearray(2048 * 24)
    pvd = bytearray(2048)
    pvd[0], pvd[1:6] = 1, b'CD001'
    pvd[156:156 + 34] = record(18, 2048, '\x00', True)[:34].ljust(34, b'\0')
    image[16 * 2048:17 * 2048] = pvd
    image[18 * 2048:18 * 2048 + 64] = record(19, 2048, 'PS3_GAME', True).ljust(64, b'\0')
    image[19 * 2048:19 * 2048 + 64] = record(20, len(data), name + ';1', False).ljust(64, b'\0')
    image[20 * 2048:20 * 2048 + len(data)] = data
    return bytes(image)


class PlayerArtTest(unittest.TestCase):
    def test_game_folder_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / 'game/PS3_GAME'
            game.mkdir(parents=True)
            (game / 'PIC1.PNG').write_bytes(png(1920, 1080))
            self.assertEqual(find_title_art(Path(tmp) / 'home', Path(tmp) / 'game'), game / 'PIC1.PNG')

    def test_small_picture_is_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / 'PS3_GAME'
            game.mkdir()
            (game / 'PIC1.PNG').write_bytes(png(320, 176))
            self.assertIsNone(find_title_art(tmp, tmp))

    def test_extracted_from_iso_and_cached(self):
        with tempfile.TemporaryDirectory() as tmp:
            iso = Path(tmp) / 'game.iso'
            iso.write_bytes(iso_with('PIC1.PNG', png(1280, 720)))
            found = find_title_art(Path(tmp) / 'home', None, iso)
            self.assertEqual(found, Path(tmp) / 'home/player-art/PIC1.PNG')
            self.assertEqual(found.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')

    def test_nothing_available(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(find_title_art(tmp, None, None))


if __name__ == '__main__':
    unittest.main()
