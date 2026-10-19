import csv
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from performance_report import BASE_COLUMNS, COLUMNS, DETAIL_COLUMNS, LOOP_COLUMNS, REASON_LOOP_COLUMNS, STATE_LOOP_COLUMNS, PerformanceSession, analyze, analyze_fifo_samples, analyze_render_loop, clean_environment


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
    def test_no_flip_state_report_validation_and_packaging(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = PerformanceSession(Path(tmp), 'test', {})
            write_csv(session.csv)
            path = session.directory / 'frames.csv.loop.csv'
            row = dict.fromkeys(STATE_LOOP_COLUMNS, 0)
            row.update(frame=0, stage='mothman', interval_ms=20, wait_ms=16,
                       fifo_ms=3, present_ms=1, wait_calls=2,
                       wait_no_pending_flip_ms=16, wait_no_pending_flip_calls=2,
                       no_flip_host_empty_ms=10, no_flip_host_empty_calls=1,
                       no_flip_guest_put_get_differ_ms=6, no_flip_guest_put_get_differ_calls=1,
                       wait_event_wakes=1, wait_timeouts=1)
            def write():
                with path.open('w', newline='') as f:
                    writer = csv.DictWriter(f, fieldnames=STATE_LOOP_COLUMNS)
                    writer.writeheader(); writer.writerow(row)
            write(); report = analyze_render_loop(path)
            self.assertTrue(report['no_flip_state_detail_available'])
            self.assertEqual(report['stages']['mothman']['no_flip_entry_states']['host_empty'],
                             {'calls': 1, 'total_ms': 10})
            with zipfile.ZipFile(session.finish(Path(tmp))) as z:
                self.assertTrue(json.loads(z.read('summary.json'))['render_loop']['no_flip_state_detail_available'])
                self.assertIn('frames.csv.loop.csv', z.namelist())
            for invalid in (-1, 2**32, 'nan'):
                row['no_flip_host_empty_calls'] = invalid; write()
                with self.assertRaises(ValueError): analyze_render_loop(path)
            row['no_flip_host_empty_calls'] = 1
            row['no_flip_host_empty_ms'] = 'nan'; write()
            with self.assertRaises(ValueError): analyze_render_loop(path)

    def test_wait_reason_report_and_legacy_availability(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'loop.csv'
            row = dict.fromkeys(REASON_LOOP_COLUMNS, 0)
            row.update(frame=0, stage='mothman', interval_ms=20, wait_ms=16,
                       fifo_ms=3, present_ms=1, wait_calls=2,
                       wait_no_pending_flip_ms=10, wait_clock_gate_ms=6,
                       wait_no_pending_flip_calls=1, wait_clock_gate_calls=1,
                       wait_event_wakes=1, wait_timeouts=1)
            def write():
                with path.open('w', newline='') as f:
                    writer=csv.DictWriter(f, fieldnames=REASON_LOOP_COLUMNS)
                    writer.writeheader();writer.writerow(row)
            write();report=analyze_render_loop(path)
            self.assertTrue(report['wait_reason_detail_available'])
            self.assertFalse(report['no_flip_state_detail_available'])
            self.assertEqual(report['stages']['mothman']['classified_event_waits']['clock_gate'],
                             {'calls': 1, 'total_ms': 6})
            row['wait_clock_gate_calls']=-1;write()
            with self.assertRaises(ValueError): analyze_render_loop(path)
    def test_render_loop_sections_and_packaging(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = PerformanceSession(Path(tmp), 'test', {})
            write_csv(session.csv)
            path = session.directory / 'frames.csv.loop.csv'
            def write(**changes):
                row = dict(frame=0, stage='mothman', interval_ms=33, wait_ms=10,
                           tick_ms=2, present_ms=12, fifo_ms=5, pump_ms=1, other_ms=3,
                           max_wait_ms=9, max_wait_target_ms=7, wait_calls=2, late_waits=1, vblank=99)
                row.update(changes)
                with path.open('w', encoding='utf-8', newline='') as f:
                    writer = csv.DictWriter(f, fieldnames=LOOP_COLUMNS)
                    writer.writeheader(); writer.writerow(row)
            write()
            report = analyze_render_loop(path)
            self.assertFalse(report['wait_reason_detail_available'])
            stage = report['stages']['mothman']
            self.assertEqual(stage['maximum_section_sum_error_ms'], 0)
            self.assertEqual(stage['host_timings_ms']['present_ms']['mean'], 12)
            self.assertEqual(stage['waits_exceeding_requested_plus_1ms'], 1)
            with zipfile.ZipFile(session.finish(Path(tmp))) as z:
                self.assertIn('frames.csv.loop.csv', z.namelist())
                self.assertEqual(json.loads(z.read('summary.json'))['render_loop']['samples'], 1)
            for invalid in ({'wait_ms': 'nan'}, {'frame': 2}, {'stage': 'save'},
                            {'late_waits': 3}, {'vblank': -1}, {'present_ms': -1}):
                write(**invalid)
                with self.assertRaises(ValueError): analyze_render_loop(path)

    def test_invalid_loop_report_does_not_block_main_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = PerformanceSession(Path(tmp), 'test', {})
            write_csv(session.csv)
            (session.directory / 'frames.csv.loop.csv').write_text('invalid')
            with zipfile.ZipFile(session.finish(Path(tmp))) as z:
                self.assertNotIn('frames.csv.loop.csv', z.namelist())
                self.assertEqual(json.loads(z.read('summary.json'))['render_loop']['status'], 'invalid-loop-report')
    def test_sampled_callbacks_are_validated_and_packaged(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = PerformanceSession(Path(tmp), 'test', {})
            write_csv(session.csv)
            path = session.directory / 'frames.csv.fifo.json'
            data = {'schema': 1, 'scope': 'all-marked-stages-combined',
                    'sampling': 'lcg-high-byte-zero', 'nominal_sample_rate': 256,
                    'inclusive_categories': True, 'categories': [
                        {'name': name, 'calls': 1000, 'samples': 4, 'sampled_ms': 2}
                        for name in ('vertex_shader', 'constants', 'bind_texture', 'draw_arrays', 'draw_indexed', 'vertex_fetch')]}
            path.write_text(json.dumps(data))
            report = analyze_fifo_samples(path)
            self.assertEqual(report['categories'][0]['estimated_total_ms'], 500)
            self.assertFalse(report['categories'][0]['sample_count_at_least_30'])
            with zipfile.ZipFile(session.finish(Path(tmp))) as z:
                self.assertIn('frames.csv.fifo.json', z.namelist())
                self.assertEqual(json.loads(z.read('summary.json'))['sampled_fifo_callbacks']['status'], 'sampled')
            data['categories'][0]['samples'] = 1001
            path.write_text(json.dumps(data))
            with self.assertRaises(ValueError): analyze_fifo_samples(path)
            data['categories'][0].update(samples=0, sampled_ms=0)
            path.write_text(json.dumps(data))
            self.assertIsNone(analyze_fifo_samples(path)['categories'][0]['estimated_total_ms'])
            path.write_text('x' * 65537)
            with self.assertRaises(ValueError): analyze_fifo_samples(path)

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
            self.assertFalse(report['fifo_lock_detail_available'])
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

    def test_fifo_counts_and_previous_detail_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'
            write_csv(path, (16, 24), fifo_lock_wait_ms=2, fifo_lock_calls=4,
                      fifo_flip_held_calls=1)
            report = analyze(path)
            self.assertTrue(report['fifo_lock_detail_available'])
            self.assertEqual(report['stages']['castle']['fifo_call_counts'],
                             {'fifo_lock_calls': 8, 'fifo_flip_held_calls': 2})
            self.assertEqual(report['stages']['castle']['host_timings_ms']['fifo_lock_wait_ms']['mean'], 2)
            with path.open('w', encoding='utf-8', newline='') as output:
                writer = csv.DictWriter(output, fieldnames=DETAIL_COLUMNS)
                writer.writeheader()
                row = dict.fromkeys(DETAIL_COLUMNS, 0)
                row.update(frame=0, stage='mothman', interval_ms=20)
                writer.writerow(row)
            report = analyze(path)
            self.assertTrue(report['preparation_detail_available'])
            self.assertFalse(report['fifo_lock_detail_available'])
            self.assertNotIn('fifo_call_counts', report['stages']['mothman'])

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
                csv.writer(f).writerow((1, 'mothman', 40, 8, 0, 0, 1, 2, 3, 2, 4, 1, 1) + (0,) * (len(COLUMNS) - len(BASE_COLUMNS)))
            report = analyze(path)
            self.assertEqual(report['stages']['mothman']['average_present_callback_fps'], 25)
            self.assertEqual(report['stages']['castle']['samples'], 1)

    def test_context_keeps_real_neighbors_across_stage_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frames.csv'
            write_csv(path, (16, 80), texture_create_calls=2)
            with path.open('a', encoding='utf-8', newline='') as f:
                csv.writer(f).writerow((2, 'mothman', 20, 8, 0, 0, 1, 2, 3, 2, 4, 1, 1) + (0,) * (len(COLUMNS) - len(BASE_COLUMNS)))
            report = analyze(path)
            stage = report['stages']['castle']
            self.assertEqual(stage['preparation_calls_per_1000_frames']['texture_create_calls'], 2000)
            context = stage['slow_frame_context'][0]
            self.assertEqual(context['peak_frame'], 1)
            self.assertEqual([r['frame'] for r in context['frames']], [0, 1, 2])
            self.assertEqual(context['frames'][-1]['stage'], 'mothman')
            self.assertEqual(report['stages']['mothman']['samples'], 1)

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
            self.assertEqual(env['D2_LOOP_FRAME'], '1')
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
                                            'D2_FIFO_FRAME_TIME': '1', 'd2_loop_frame': '1', 'keep': 'yes'}), {'keep': 'yes'})


if __name__ == '__main__': unittest.main()
