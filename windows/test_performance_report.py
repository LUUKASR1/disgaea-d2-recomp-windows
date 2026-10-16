import csv
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from performance_report import BASE_COLUMNS, COLUMNS, PerformanceSession, analyze, clean_environment


def write_csv(path, intervals=(10, 20, 30, 40), **overrides):
    with path.open('w', encoding='utf-8', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=COLUMNS)
        writer.writeheader()
        for frame, interval in enumerate(intervals):
            row = dict.fromkeys(COLUMNS, 0)
            row.update(frame=frame, stage='castle', interval_ms=interval,
                       render_host_ms=8, fifo_host_ms=3, wait_fence_host_ms=2,
                       prepare_host_ms=4, present_host_ms=1, wait_fence_calls=1,
                       pipeline_misses=1 if frame == 0 else 0,
                       pipeline_compile_ms=2 if frame == 0 else 0)
            row.update(overrides)
            writer.writerow(row)


class PerformanceTests(unittest.TestCase):
    def test_weighted_frame_rate_percentiles_and_compilation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'; write_csv(path)
            report = analyze(path)
            stage = report['stages']['castle']
            self.assertEqual(report['status'], 'measured')
            self.assertEqual(stage['average_present_callback_fps'], 40)
            self.assertEqual(stage['frame_ms']['p50'], 20)
            self.assertEqual(stage['frame_ms']['p99'], 40)
            self.assertEqual(stage['frames_over_33_34ms'], 1)
            self.assertEqual(stage['pipeline_misses_total'], 1)
            self.assertEqual(stage['pipeline_compile_ms_total'], 2)
            self.assertEqual(stage['wait_fence_calls_total'], 4)
            self.assertFalse(report['gpu_timestamps_available'])
            self.assertFalse(report['whole_game_cpu_time_available'])
            self.assertTrue(report['preparation_detail_available'])
            self.assertEqual(stage['slowest_frames'][0]['frame'], 3)

    def test_detail_counters_and_legacy_compatibility(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'
            write_csv(path, (20, 40), resource_create_host_ms=3, texture_create_calls=2,
                      buffer_create_calls=1, shader_compile_calls=1, pso_create_calls=4)
            stage = analyze(path)['stages']['castle']
            self.assertEqual(stage['host_timings_ms']['resource_create_host_ms']['maximum'], 3)
            self.assertEqual(stage['preparation_call_counts']['texture_create_calls'], 4)
            with path.open('w', encoding='utf-8', newline='') as output:
                writer = csv.writer(output); writer.writerow(BASE_COLUMNS)
                writer.writerow((0, 'castle', 20, 8, 0, 0, 1, 2, 3, 2, 4, 1, 1))
            report = analyze(path)
            self.assertFalse(report['preparation_detail_available'])
            self.assertNotIn('preparation_call_counts', report['stages']['castle'])

    def test_invalid_times_stages_sequence_and_counters_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'
            for invalid in ({'interval_ms': 'nan'}, {'render_host_ms': 'inf'},
                            {'interval_ms': 0}, {'interval_ms': '1e308'}, {'stage': 'unknown'}, {'frame': 3},
                            {'pipeline_misses': -1}, {'wait_fence_calls': 2**32},
                            {'pso_create_failures': -1}, {'resource_create_host_ms': 'nan'}):
                with self.subTest(invalid=invalid):
                    write_csv(path, **invalid)
                    with self.assertRaises(ValueError): analyze(path)

    def test_limit_incomplete_row_and_legacy_format_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'
            path.write_text('frame,stage\n0,castle\n')
            with self.assertRaises(ValueError): analyze(path)
            write_csv(path)
            with path.open('a') as f: f.write('4,castle\n')
            with self.assertRaises(ValueError): analyze(path)
            write_csv(path, (16,) * 18001)
            with self.assertRaises(ValueError): analyze(path)

    def test_groups_do_not_mix_scene_intervals(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'; write_csv(path, (16,))
            with path.open('a', encoding='utf-8', newline='') as f:
                csv.writer(f).writerow((1, 'mothman', 40, 8, 0, 0, 1, 2, 3, 2, 4, 1, 1) + (0,) * 12)
            report = analyze(path)
            self.assertEqual(report['stages']['mothman']['average_present_callback_fps'], 25)
            self.assertEqual(report['stages']['castle']['samples'], 1)

    def test_session_is_unique_unicode_safe_and_excludes_heavy_profiling(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'área do usuário'
            a = PerformanceSession(home, 'test', {'resolution': '1280x720'})
            b = PerformanceSession(home, 'test', {})
            self.assertNotEqual(a.csv, b.csv)
            env = a.environment({'PERF': '1', 'D2_FIFO_DETAIL': '1', 'Path': 'keep', 'd2_perf_csv': 'stale'})
            self.assertNotIn('PERF', env)
            self.assertNotIn('D2_FIFO_DETAIL', env)
            self.assertNotIn('d2_perf_csv', env)
            self.assertEqual(env['Path'], 'keep')
            self.assertEqual(env['D2_PERF_CSV'], str(a.csv))
            self.assertEqual(env['D2_FRAME_LIGHT'], '1')
            write_csv(a.csv)
            report = a.finish(Path(tmp))
            self.assertEqual(a.finish(Path(tmp)), report)
            with zipfile.ZipFile(report) as z:
                self.assertEqual(set(z.namelist()), {'frames.csv', 'summary.json', 'session.json'})
                self.assertEqual(json.loads(z.read('summary.json'))['samples'], 4)

    def test_no_measurement_is_not_reported_as_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = PerformanceSession(Path(tmp), 'test', {})
            report = session.finish(Path(tmp))
            with zipfile.ZipFile(report) as z:
                self.assertEqual(json.loads(z.read('summary.json'))['status'], 'no-samples')
                self.assertNotIn('frames.csv', z.namelist())

    def test_disabled_environment_drops_inherited_measurement_flags(self):
        self.assertEqual(clean_environment({'d2_frame_light': '1', 'D2_PERF_CSV': 'stale',
                                            'D2_FIFO_FRAME_TIME': '1', 'keep': 'yes'}), {'keep': 'yes'})


if __name__ == '__main__': unittest.main()
