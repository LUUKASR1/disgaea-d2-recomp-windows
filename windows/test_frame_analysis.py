"""Asset-free checks preventing simplistic shader/depth-only HUD classification."""
import importlib.util
import math
from pathlib import Path
import unittest

module = Path(__file__).resolve().parents[1] / 'tools/analyze_frame_capture.py'
spec = importlib.util.spec_from_file_location('frame_analysis', module)
analysis = importlib.util.module_from_spec(spec); spec.loader.exec_module(analysis)


def record(index, shader='2003CB89', z=.95, target='00000000'):
    return dict(index=index, draw=True, vs=shader, target=target, vertices=6,
                positions=[(0.,0.,z,1.)]*3)


class HUDAnalysisTests(unittest.TestCase):
    def test_particle_and_character_using_hud_shader_remain_in_scene(self):
        draws=[record(0,z=.999), record(1,'99362655',z=.1), record(2,z=.95)]
        result=analysis.summarize(draws)
        self.assertEqual(result['candidate_hud_start'],2)
        self.assertEqual(result['same_screen_shader_before_boundary'],1)

    def test_reject_missing_invalid_geometry_and_offscreen_target(self):
        for value in [math.nan,math.inf,-.1,.99]:
            self.assertFalse(analysis.is_hud_candidate(record(0,z=value)))
        self.assertFalse(analysis.is_hud_candidate(record(0,target='00100000')))
        missing=record(0);missing['positions']=[]
        self.assertFalse(analysis.is_hud_candidate(missing))

    def test_scene_draw_after_candidate_prevents_moving_early_draw(self):
        result=analysis.summarize([record(0),record(1,z=.999),record(2)])
        self.assertEqual(result['candidate_hud_start'],2)
        self.assertEqual(result['nonterminal_candidates'],[0])

    def test_title_is_not_automatically_split(self):
        result=analysis.summarize([record(0,'0E216F34'),record(1,'0E216F34')])
        self.assertTrue(result['title_only'])
        self.assertIsNone(result['candidate_hud_start'])

    def test_manifest_parser_keeps_runtime_shader_and_vertex_samples(self):
        text='''op003 DRAW  rt=0x00000000 fp=0x00000001 n=6
      SHADER vs-record-hash=2003CB89
      FONT v0 pos=(-1,1,0.95,1)
      FONT v1 pos=(1,1,0.95,1)
      FONT v2 pos=(-1,-1,0.95,1)
'''
        parsed=analysis.parse_manifest(text)
        self.assertEqual(parsed[0]['index'],3)
        self.assertEqual(analysis.summarize(parsed)['candidate_hud_draws'],1)


if __name__ == '__main__': unittest.main()
