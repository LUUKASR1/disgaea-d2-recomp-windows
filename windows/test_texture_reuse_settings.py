import unittest
from texture_reuse_settings import texture_reuse_environment


class TextureReuseSettingsTests(unittest.TestCase):
    def test_development_requires_explicit_opt_in_and_cleans_inherited_flags(self):
        inherited = {'d2_texture_reuse': '1', 'D2_TEXTURE_REUSE': '1', 'Path': 'keep'}
        self.assertEqual(texture_reuse_environment(inherited), {'D2_TEXTURE_REUSE': '0', 'Path': 'keep'})
        self.assertEqual(texture_reuse_environment(inherited, True), {'D2_TEXTURE_REUSE': '1', 'Path': 'keep'})
        self.assertEqual(inherited['d2_texture_reuse'], '1')

    def test_official_channel_always_disables_the_experiment(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                self.assertEqual(texture_reuse_environment({'D2_TEXTURE_REUSE': '1'}, enabled, True),
                                 {'D2_TEXTURE_REUSE': '0'})


if __name__ == '__main__': unittest.main()
