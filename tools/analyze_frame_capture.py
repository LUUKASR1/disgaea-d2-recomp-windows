"""Inspect a capture ZIP without extracting game payloads or changing rendering.

The HUD boundary is an observational hypothesis for the audited USA 1.40 build,
not authorization to move draws. Only three vertex samples exist per draw.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import zipfile

GAMEPLAY_SCREEN_VS = '2003CB89'
TITLE_SCREEN_VS = '0E216F34'


def parse_manifest(text):
    records = []
    for block in re.split(r'(?=^op\d+ )', text, flags=re.M)[1:]:
        first = block.splitlines()[0]
        def value(pattern):
            match = re.search(pattern, block)
            return match[1] if match else None
        index = int(value(r'^op(\d+)'))
        positions = []
        for row in re.findall(r'FONT v\d pos=\(([^)]+)\)', block):
            try:
                point = tuple(float(item) for item in row.split(','))
                if len(point) == 4: positions.append(point)
            except ValueError:
                pass
        records.append(dict(index=index, draw=' DRAW ' in first,
            target=value(r'rt=0x([0-9A-F]+)'),
            vs=value(r'vs-record-hash=([0-9A-F]+)'),
            fp=value(r'fp=0x([0-9A-F]+)'),
            vertices=int(value(r' n=(\d+)') or 0),
            positions=positions,
            textures=[dict(unit=int(unit), raw=raw, width=int(width), height=int(height), format=fmt)
                for unit, present, raw, width, height, fmt in re.findall(
                    r'BATCH tex(\d) set=(\d) ea=[0-9A-F]+ raw=([0-9A-F]+) size=(\d+)x(\d+) fmt=([0-9A-F]+)', block)
                if present == '1']))
    return records


def is_hud_candidate(record):
    points = record['positions']
    return bool(record['draw'] and record['target'] == '00000000' and
        record['vs'] == GAMEPLAY_SCREEN_VS and len(points) == 3 and
        all(all(math.isfinite(v) for v in p) and abs(p[3]-1) < 1e-5 and
            0 <= p[2] < 0.99 for p in points))


def summarize(records):
    suffix = []
    for record in reversed(records):
        if not is_hud_candidate(record): break
        suffix.append(record)
    suffix.reverse()
    outside = records[:-len(suffix)] if suffix else records
    return dict(draw_records=len(records),
        vertex_shaders=dict(Counter(r['vs'] or 'unknown' for r in records)),
        candidate_hud_start=suffix[0]['index'] if suffix else None,
        candidate_hud_draws=len(suffix),
        candidate_hud_vertices=sum(r['vertices'] for r in suffix),
        same_screen_shader_before_boundary=sum(r['vs'] == GAMEPLAY_SCREEN_VS for r in outside),
        nonterminal_candidates=[r['index'] for r in outside if is_hud_candidate(r)],
        title_only=bool(records and all(r['vs'] == TITLE_SCREEN_VS for r in records)),
        status='hypothesis-needs-isolated-replay',
        limitation='Three vertex samples per draw; full geometry and final GPU coverage are not captured.')


def analyze(archive_path):
    frames = []
    with zipfile.ZipFile(archive_path) as archive:
        manifests = sorted(n for n in archive.namelist() if n.endswith('/manifest.txt'))
        for name in manifests:
            if archive.getinfo(name).file_size > 32 * 1024 * 1024:
                raise ValueError('Manifest exceeds diagnostic size limit')
            marker = name.rsplit('/', 1)[0] + '/complete.json'
            if marker not in archive.namelist():
                frames.append(dict(frame=name.split('/')[-2], status='incomplete')); continue
            complete = json.loads(archive.read(marker))
            if complete.get('schema') != 1 or complete.get('status') != 'complete':
                frames.append(dict(frame=name.split('/')[-2], status='incomplete')); continue
            result = summarize(parse_manifest(archive.read(name).decode('utf-8')))
            result.update(frame=name.split('/')[-2], frame_number=complete['frame'],
                render_size=[complete['render_width'], complete['render_height']])
            frames.append(result)
    return dict(schema=1, rendering_changed=False, frames=frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_zip', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    text = json.dumps(analyze(args.capture_zip), indent=2, ensure_ascii=False)
    if args.output: args.output.write_text(text+'\n', encoding='utf-8')
    else: print(text)


if __name__ == '__main__': main()
