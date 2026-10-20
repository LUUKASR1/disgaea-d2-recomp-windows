import unittest
from texture_reuse_settings import texture_reuse_environment, vertex_context_environment, ready_wake_environment


class TextureReuseSettingsTests(unittest.TestCase):
    def test_ready_wake_channel_and_inherited_flags(self):
        inherited = {'d2_gcm_ready_wake': '1', 'D2_GCM_READY_WAKE': '1',
                     'D2_GCM_UNLIMITED': '0', 'D2_VERTEX_CONTEXT': '0'}
        before = dict(inherited)
        for enabled, official, expected in ((False, False, '0'), (True, False, '1'),
                                            (True, True, '0'), (False, True, '0')):
            result = ready_wake_environment(inherited, enabled, official)
            self.assertNotIn('d2_gcm_ready_wake', result)
            self.assertEqual(result['D2_GCM_READY_WAKE'], expected)
            self.assertEqual(result['D2_GCM_UNLIMITED'], '0')
            self.assertEqual(result['D2_VERTEX_CONTEXT'], '0')
        self.assertEqual(inherited, before)

    def test_vertex_preparation_is_independent_opt_in(self):
        inherited = {'d2_vertex_context': '1', 'D2_VERTEX_CONTEXT': '1', 'D2_TEXTURE_REUSE': '1'}
        for enabled, official, expected in ((False, False, '0'), (True, False, '1'),
                                            (True, True, '0'), (False, True, '0')):
            result = vertex_context_environment(inherited, enabled, official)
            self.assertNotIn('d2_vertex_context', result)
            self.assertEqual(result['D2_VERTEX_CONTEXT'], expected)
            self.assertEqual(result['D2_TEXTURE_REUSE'], '1')
        self.assertIn('d2_vertex_context', inherited)
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
