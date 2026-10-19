"""Bounded host-frame measurements; no image captures or GPU-time inference."""
import csv
from datetime import datetime
import json
import math
from pathlib import Path
import uuid
import zipfile

LIMIT = 18000
MAX_CSV_BYTES = 8 * 1024 * 1024
BASE_COLUMNS = ('frame', 'stage', 'interval_ms', 'render_host_ms', 'pipeline_compile_ms',
           'pipeline_misses', 'pipeline_cache_entries', 'draws', 'fifo_host_ms',
           'wait_fence_host_ms', 'prepare_host_ms', 'present_host_ms', 'wait_fence_calls')
STAGES = {'castle', 'batalha', 'mothman'}
TIMES = ('render_host_ms', 'pipeline_compile_ms', 'fifo_host_ms',
         'wait_fence_host_ms', 'prepare_host_ms', 'present_host_ms')
COUNTS = ('pipeline_misses', 'pipeline_cache_entries', 'draws', 'wait_fence_calls')
DETAIL_TIMES = ('prepare_setup_ms', 'prepare_targets_ms', 'prepare_draws_ms',
                'resource_create_host_ms', 'shader_compile_host_ms', 'pso_create_host_ms')
DETAIL_COUNTS = ('texture_create_calls', 'buffer_create_calls', 'resource_create_failures',
                 'shader_compile_calls', 'pso_create_calls', 'pso_create_failures')
DETAIL_COLUMNS = BASE_COLUMNS + DETAIL_TIMES + DETAIL_COUNTS
FIFO_TIMES = ('fifo_lock_wait_ms',)
FIFO_COUNTS = ('fifo_lock_calls', 'fifo_flip_held_calls')
COLUMNS = DETAIL_COLUMNS + FIFO_TIMES + FIFO_COUNTS
ENVIRONMENT = {'D2_FRAME_LIGHT', 'D2_PERF_CSV', 'D2_FIFO_FRAME_TIME', 'D2_LOOP_FRAME'}
LOOP_SECTIONS = ('wait_ms', 'tick_ms', 'present_ms', 'fifo_ms', 'pump_ms', 'other_ms')
LOOP_TIMES = ('interval_ms', *LOOP_SECTIONS, 'max_wait_ms', 'max_wait_target_ms')
LOOP_COUNTS = ('wait_calls', 'late_waits', 'vblank')
LOOP_COLUMNS = ('frame', 'stage', *LOOP_TIMES, *LOOP_COUNTS)
WAIT_REASONS = ('no_pending_flip', 'fifo_pending', 'ticket_order', 'clock_gate', 'eligible_at_entry')
WAIT_TIMES = tuple('wait_' + name + '_ms' for name in WAIT_REASONS)
WAIT_COUNTS = tuple('wait_' + name + '_calls' for name in WAIT_REASONS)
WAIT_OUTCOMES = ('wait_event_wakes', 'wait_timeouts', 'wait_other_results')
REASON_LOOP_COLUMNS = LOOP_COLUMNS + WAIT_TIMES + WAIT_COUNTS + WAIT_OUTCOMES
NO_FLIP_STATES = ('label_wait', 'host_queued', 'guest_put_get_differ', 'host_empty', 'legacy')
NO_FLIP_TIMES = tuple('no_flip_' + name + '_ms' for name in NO_FLIP_STATES)
NO_FLIP_COUNTS = tuple('no_flip_' + name + '_calls' for name in NO_FLIP_STATES)
STATE_LOOP_COLUMNS = REASON_LOOP_COLUMNS + NO_FLIP_TIMES + NO_FLIP_COUNTS


def clean_environment(original):
    return {k: v for k, v in original.items() if k.upper() not in ENVIRONMENT}


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def analyze_render_loop(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_CSV_BYTES:
        raise ValueError('Invalid render loop report')
    groups = {}
    with path.open(encoding='utf-8', newline='') as source:
        reader = csv.DictReader(source)
        columns = tuple(reader.fieldnames or ())
        if columns not in (LOOP_COLUMNS, REASON_LOOP_COLUMNS, STATE_LOOP_COLUMNS):
            raise ValueError('Invalid render loop columns')
        states = columns == STATE_LOOP_COLUMNS
        reasons = columns != LOOP_COLUMNS
        times = LOOP_TIMES + (WAIT_TIMES if reasons else ()) + (NO_FLIP_TIMES if states else ())
        counts = LOOP_COUNTS + (WAIT_COUNTS + WAIT_OUTCOMES if reasons else ()) + (NO_FLIP_COUNTS if states else ())
        for index, raw in enumerate(reader):
            if (index >= LIMIT or None in raw or any(v is None for v in raw.values())
                    or int(raw['frame']) != index or raw['stage'] not in STAGES):
                raise ValueError('Invalid render loop row')
            row = {k: float(raw[k]) for k in times}
            if any(not math.isfinite(v) or not 0 <= v <= 1e12 for v in row.values()) or row['interval_ms'] <= 0:
                raise ValueError('Invalid render loop timing')
            row.update({k: int(raw[k]) for k in counts})
            if states and any(not 0 <= row[k] <= 0xffffffff for k in NO_FLIP_COUNTS):
                raise ValueError('Invalid no-flip state counters')
            if reasons and any(not 0 <= row[k] <= 0xffffffff for k in WAIT_COUNTS + WAIT_OUTCOMES):
                raise ValueError('Invalid wait reason counters')
            if (not 0 <= row['wait_calls'] <= 0xffffffff or
                    not 0 <= row['late_waits'] <= row['wait_calls'] or not 0 <= row['vblank'] <= 0xffffffffffffffff):
                raise ValueError('Invalid render loop counters')
            row['frame'] = index
            groups.setdefault(raw['stage'], []).append(row)
    stages = {}
    for stage, rows in groups.items():
        stages[stage] = {
            'samples': len(rows),
            'host_timings_ms': {k: {'mean': sum(r[k] for r in rows) / len(rows),
                                    'p95': percentile([r[k] for r in rows], .95),
                                    'maximum': max(r[k] for r in rows)} for k in times},
            'wait_calls': sum(r['wait_calls'] for r in rows),
            'waits_exceeding_requested_plus_1ms': sum(r['late_waits'] for r in rows),
            'maximum_section_sum_error_ms': max(abs(sum(r[k] for k in LOOP_SECTIONS) - r['interval_ms']) for r in rows),
            'slowest_intervals': sorted(rows, key=lambda r: r['interval_ms'], reverse=True)[:10]}
        if reasons:
            stages[stage]['classified_event_waits'] = {
                name: {'calls': sum(r[count] for r in rows), 'total_ms': sum(r[time] for r in rows)}
                for name, time, count in zip(WAIT_REASONS, WAIT_TIMES, WAIT_COUNTS)}
            stages[stage]['event_wait_outcomes'] = {k: sum(r[k] for r in rows) for k in WAIT_OUTCOMES}
        if states:
            stages[stage]['no_flip_entry_states'] = {
                name: {'calls': sum(r[count] for r in rows), 'total_ms': sum(r[time] for r in rows)}
                for name, time, count in zip(NO_FLIP_STATES, NO_FLIP_TIMES, NO_FLIP_COUNTS)}
    return {'schema': 1, 'status': 'measured' if stages else 'no-samples', 'stages': stages,
            'no_flip_state_detail_available': states,
            'wait_reason_detail_available': reasons,
            'samples': sum(len(rows) for rows in groups.values()),
            'limitations': ['Render-loop intervals end after guest submission; they are not visible-render callback intervals.',
                           'Frame indices and keyboard marker polling are independent of frames.csv; never join them by row number.',
                           'Wait time includes intentional idle and host scheduling, not CPU utilization or proven wasted time.',
                           'Present time includes the whole guest submission, not only DXGI Present.',
                           'Late waits exceed the requested milliseconds by more than 1ms; they do not prove missed display refreshes.',
                           'Wait reasons are snapshots under the flip lock before the original event wait; state can change immediately afterward.',
                           'Reason durations cover completed event waits, not how long a condition remained true; atomic counters have approximate cross-thread attribution.',
                           'These sequential loop sections are separate from and may overlap backend or sampled-callback categories.',
                           'No-flip substates are separate entry loads: label flag first, then host slots, then guest PUT/GET; not a coherent producer snapshot.',
                           'No-flip timings are a subset of no_pending_flip waits, not producer CPU time, proof of a stall or condition duration.',
                           'Host-empty can include ordinary idle; PUT/GET differences do not establish why copying or publication has not completed.',
                           'Host-loop instrumentation overhead has not been measured on the target PC.']}


def analyze_fifo_samples(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 65536:
        raise ValueError('Invalid callback sample report')
    data = json.loads(path.read_text(encoding='utf-8'))
    names = ('vertex_shader', 'constants', 'bind_texture', 'draw_arrays', 'draw_indexed', 'vertex_fetch')
    if (data.get('schema') != 1 or data.get('scope') != 'all-marked-stages-combined'
            or data.get('sampling') != 'lcg-high-byte-zero'
            or data.get('nominal_sample_rate') != 256 or data.get('inclusive_categories') is not True
            or not isinstance(data.get('categories'), list) or len(data['categories']) != 6):
        raise ValueError('Invalid callback sample schema')
    categories = []
    for name, row in zip(names, data['categories']):
        calls, samples, ms = row['calls'], row['samples'], row['sampled_ms']
        if (row['name'] != name or type(calls) is not int or type(samples) is not int
                or not 0 <= samples <= calls <= 10**12 or type(ms) not in (int, float)
                or not math.isfinite(ms) or not 0 <= ms <= 1e12 or (samples == 0 and ms != 0)):
            raise ValueError('Invalid callback sample counters')
        categories.append({**row, 'sampled_mean_us': ms * 1000 / samples if samples else None,
                           'estimated_total_ms': ms * calls / samples if samples else None,
                           'sample_count_at_least_30': samples >= 30})
    return {**data, 'status': 'sampled', 'categories': categories,
            'limitations': ['Inclusive nested categories must not be added together.',
                            'Estimates are extrapolated host wall times, not CPU utilization or GPU time.',
                            'All marked stages are combined; this is not per-frame attribution.',
                            'Deterministic sampling may miss rare costs; counts below 30 are especially weak evidence.',
                            'The callback still in progress when saving is not a completed sample in that snapshot.',
                            'Sampling overhead has not been measured on the target PC.']}


def analyze(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_CSV_BYTES:
        raise ValueError('CSV de desempenho ausente, inválido ou grande demais.')
    groups = {}
    all_rows = []
    with path.open(encoding='utf-8', newline='') as source:
        reader = csv.DictReader(source)
        columns = tuple(reader.fieldnames or ())
        if columns not in (BASE_COLUMNS, DETAIL_COLUMNS, COLUMNS):
            raise ValueError('Formato de medição incompatível com esta versão.')
        detail = columns != BASE_COLUMNS
        fifo_detail = columns == COLUMNS
        times = TIMES + (DETAIL_TIMES if detail else ()) + (FIFO_TIMES if fifo_detail else ())
        counts = COUNTS + (DETAIL_COUNTS if detail else ()) + (FIFO_COUNTS if fifo_detail else ())
        for index, raw in enumerate(reader):
            if index >= LIMIT or None in raw or any(v is None for v in raw.values()):
                raise ValueError('CSV fora do limite ou com colunas incompletas.')
            if int(raw['frame']) != index or raw['stage'] not in STAGES:
                raise ValueError('Sequência ou cenário de medição inválido.')
            row = {k: float(raw[k]) for k in ('interval_ms', *times)}
            if any(not math.isfinite(v) or v < 0 or v > 1e12 for v in row.values()) or row['interval_ms'] <= 0:
                raise ValueError('Tempo de quadro inválido.')
            row.update({k: int(raw[k]) for k in counts})
            row['frame'] = index
            if any(not 0 <= row[k] <= 0xffffffff for k in counts):
                raise ValueError('Contador de medição inválido.')
            groups.setdefault(raw['stage'], []).append(row)
            all_rows.append((raw['stage'], row))
    stages = {}
    for stage, rows in groups.items():
        intervals = [r['interval_ms'] for r in rows]
        total = sum(intervals)
        stages[stage] = {
            'samples': len(rows), 'sampled_seconds': total / 1000,
            'average_present_callback_fps': len(rows) * 1000 / total,
            'frame_ms': {'mean': total / len(rows), 'p50': percentile(intervals, .5),
                         'p95': percentile(intervals, .95), 'p99': percentile(intervals, .99), 'maximum': max(intervals)},
            'frames_over_20ms': sum(v > 20 for v in intervals),
            'frames_over_33_34ms': sum(v > 33.34 for v in intervals),
            'frames_over_50ms': sum(v > 50 for v in intervals),
            'host_timings_ms': {k: {'mean': sum(r[k] for r in rows) / len(rows),
                                    'p95': percentile([r[k] for r in rows], .95),
                                    'maximum': max(r[k] for r in rows)} for k in times},
            'pipeline_misses_total': sum(r['pipeline_misses'] for r in rows),
            'frames_with_pipeline_misses': sum(r['pipeline_misses'] > 0 for r in rows),
            'pipeline_compile_ms_total': sum(r['pipeline_compile_ms'] for r in rows),
            'wait_fence_calls_total': sum(r['wait_fence_calls'] for r in rows),
            'draws_mean': sum(r['draws'] for r in rows) / len(rows)}
        if detail:
            stages[stage]['preparation_call_counts'] = {k: sum(r[k] for r in rows) for k in DETAIL_COUNTS}
            stages[stage]['preparation_calls_per_1000_frames'] = {
                k: sum(r[k] for r in rows) * 1000 / len(rows) for k in DETAIL_COUNTS}
        if fifo_detail:
            stages[stage]['fifo_call_counts'] = {k: sum(r[k] for r in rows) for k in FIFO_COUNTS}
        stages[stage]['slowest_frames'] = sorted([
            {'frame': r['frame'], 'interval_ms': r['interval_ms'],
             **{k: r[k] for k in (*times, *counts)}} for r in rows],
            key=lambda r: r['interval_ms'], reverse=True)[:10]
        # Post-process only, after collection. Preserve chronological neighbors
        # across stage boundaries; never mistake a stage label for an action.
        stages[stage]['slow_frame_context'] = []
        for peak in stages[stage]['slowest_frames']:
            index = peak['frame']
            stages[stage]['slow_frame_context'].append({
                'peak_frame': index,
                'frames': [dict(frame=r['frame'], stage=label,
                                interval_ms=r['interval_ms'],
                                **{k: r[k] for k in (*times, *counts)})
                           for label, r in all_rows[max(0, index - 2):index + 3]]})
    return {'schema': 2, 'purpose': 'dx12-host-frame-baseline',
            'preparation_detail_available': detail,
            'fifo_lock_detail_available': fifo_detail,
            'status': 'measured' if stages else 'no-samples',
            'samples': sum(len(rows) for rows in groups.values()), 'stages': stages,
            'gpu_timestamps_available': False, 'gpu_utilization_available': False,
            'whole_game_cpu_time_available': False, 'normal_rendering_changed': False,
            'sample_limit': LIMIT, 'percentile_method': 'nearest-rank',
            'limitations': ['Intervals are between visible render callbacks, not hardware display scanout.',
                           'Host timings are wall times, not CPU utilization or GPU execution time.',
                           'Preparation includes compilation/uploads and may overlap fence waits; do not sum categories.',
                           'FIFO time is completed-drain host time; it may overlap other threads and categories.',
                           'FIFO lock wait includes acquisition overhead; separately exchanged atomic counters have approximate frame attribution.',
                           'Flip-held calls count visits while held, not time spent held or a proven cause of frame drops.',
                           'Offscreen work is not represented by render_host_ms alone.',
                           'Compilation time tracks the SDK fragment-pipeline cache; it does not cover all shader compilation.',
                           'Detailed compile/resource/PSO call times cover this backend only and overlap preparation phases.',
                           'Creation counters count attempted calls, including failures; texture counts mean non-buffer resources.',
                           'Neighboring frames show temporal association, not causation or the identity of a game action.',
                           'Instrumentation overhead has not been measured on the target PC.']}


class PerformanceSession:
    def __init__(self, home, version, settings):
        self.home = Path(home)
        token = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
        self.directory = self.home / 'performance' / token
        self.directory.mkdir(parents=True)
        self.csv = self.directory / 'frames.csv'
        self.result = None
        (self.directory / 'session.json').write_text(json.dumps({
            'schema': 1, 'launcher': version, 'settings': settings,
            'created': datetime.now().isoformat(), 'sample_limit': LIMIT,
            'keys': {'F6': 'castle', 'F7': 'batalha', 'F9': 'mothman', 'F10': 'finish'},
            'images_captured': False, 'per_draw_profiling': True,
            'draw_profiling_mode': 'sampled-approximately-1-in-256',
            'render_loop_profiling': True,
            'graphics_api': 'd3d12'}, ensure_ascii=False, indent=2), encoding='utf-8')

    def environment(self, original):
        env = clean_environment(original)
        # Keep heavy diagnostics out even if inherited from an external shell.
        for key in list(env):
            if key.upper() in {'PERF', 'D2_FIFO_DETAIL', 'D2_FIFO_SAMPLE', 'D2_RUNTIME_TIMING'}:
                env.pop(key)
        env.update(D2_FRAME_LIGHT='1', D2_PERF_CSV=str(self.csv), D2_FIFO_FRAME_TIME='1', D2_LOOP_FRAME='1')
        return env

    def finish(self, destination=None):
        if self.result:
            return self.result
        report = analyze(self.csv) if self.csv.exists() else {
            'schema': 1, 'purpose': 'dx12-host-frame-baseline', 'status': 'no-samples',
            'samples': 0, 'stages': {}, 'gpu_timestamps_available': False,
            'message': 'Nenhum cenário marcado. Com a janela do jogo em foco, use F6/F7/F9 e depois F10.'}
        sampled_path = self.directory / 'frames.csv.fifo.json'
        loop_path = self.directory / 'frames.csv.loop.csv'
        loop_valid = False
        report['render_loop'] = {'status': 'missing-loop-report'}
        if loop_path.is_file() and not loop_path.is_symlink():
            try:
                report['render_loop'] = analyze_render_loop(loop_path)
                loop_valid = True
            except (ValueError, OSError, TypeError, KeyError):
                report['render_loop'] = {'status': 'invalid-loop-report'}
        sampled_valid = False
        if sampled_path.is_file() and not sampled_path.is_symlink():
            try:
                report['sampled_fifo_callbacks'] = analyze_fifo_samples(sampled_path)
                sampled_valid = True
            except (ValueError, OSError, TypeError, KeyError, AttributeError):
                report['sampled_fifo_callbacks'] = {'status': 'invalid-sample-report'}
        (self.directory / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        destination = Path(destination) if destination else Path.home() / 'Downloads'
        if not destination.is_dir():
            destination = self.home / 'performance/reports'
        destination.mkdir(parents=True, exist_ok=True)
        target = destination / ('DisgaeaD2-Desempenho-' + self.directory.name + '.zip')
        # Exclusive creation: never replace a user's existing report.
        with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED) as archive:
            for name in ('session.json', 'summary.json', 'frames.csv', 'frames.csv.fifo.json', 'frames.csv.loop.csv'):
                if name == 'frames.csv.fifo.json' and not sampled_valid:
                    continue
                if name == 'frames.csv.loop.csv' and not loop_valid:
                    continue
                path = self.directory / name
                if path.is_file() and not path.is_symlink():
                    archive.write(path, name)
        self.result = target
        return target
