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

    def test_cartoon_button_states_and_logo_exist_with_alpha(self):
        for name, size in (('play', (340, 84)), ('iso', (164, 60)), ('gear', (164, 60)), ('small', (170, 46))):
            for state in 'nhpd':
                data = (Path(__file__).parent / ('assets/btn-%s-%s.png' % (name, state))).read_bytes()
                self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                self.assertEqual(struct.unpack('>II', data[16:24]), size)
                self.assertIn(data[25], (4, 6))  # alpha: rounded corners
        logo = (Path(__file__).parent / 'assets/logo-disgaea.png').read_bytes()
        self.assertEqual(struct.unpack('>II', logo[16:24])[0], 336)
        self.assertIn(logo[25], (4, 6))

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
