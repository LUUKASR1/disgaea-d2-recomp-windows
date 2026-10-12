"""Verify packaged artwork formats without game files or GUI display."""
from pathlib import Path
import struct
import unittest


class BrandingTests(unittest.TestCase):
    def test_logo_keeps_alpha_and_display_proportion(self):
        data = (Path(__file__).parent / 'assets/logo-winport.png').read_bytes()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        width, height, depth, kind = struct.unpack('>IIBB', data[16:26])
        self.assertEqual((width, height), (480, 270))
        self.assertEqual(depth, 8)
        self.assertIn(kind, (4, 6))  # PNG grayscale/RGB with alpha.

    def test_icon_has_valid_small_and_large_windows_entries(self):
        data = (Path(__file__).parent / 'assets/launcher.ico').read_bytes()
        reserved, kind, count = struct.unpack('<HHH', data[:6])
        self.assertEqual((reserved, kind), (0, 1))
        sizes = set()
        for index in range(count):
            w, h, colors, unused, planes, bits, length, offset = struct.unpack('<BBBBHHII', data[6 + index * 16:22 + index * 16])
            self.assertGreater(length, 0)
            self.assertGreaterEqual(offset, 6 + count * 16)
            self.assertLessEqual(offset + length, len(data))
            self.assertEqual(w, h)
            sizes.add(w or 256)
            self.assertEqual(data[offset:offset + 8], b'\x89PNG\r\n\x1a\n')
        self.assertTrue({16, 32, 48, 64, 128, 256}.issubset(sizes))


if __name__ == '__main__':
    unittest.main()
