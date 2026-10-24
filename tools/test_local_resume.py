"""独立旧/新同源控制器续读计时；临时卷与CPU亲和不代表ESP32/TF。 / Independent old/new shared-controller resume timing; temporary-volume and CPU affinity are not ESP32/TF measurements."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import statistics
import sys
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests' / 'host'))
from test_resume import fixture as small_fixture, run
from test_local_book_index import fixture


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--before', required=True)
    p.add_argument('--after', required=True)
    p.add_argument('--font', required=True)
    p.add_argument('--out', default='build-dev/resume/performance.json')
    args = p.parse_args()
    if hasattr(os, 'sched_setaffinity'):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    report = {'scope': 'host shared controller, independent binaries; source and state on temporary volume; no panel delay',
              'before_sha': hashlib.sha256(pathlib.Path(args.before).read_bytes()).hexdigest(),
              'after_sha': hashlib.sha256(pathlib.Path(args.after).read_bytes()).hexdigest(), 'books': {}}
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        font = root / 'font.ttf'
        shutil.copyfile(args.font, font)
        small_fixture(root)
        fixture(root / 'multi.epub', 480, 6000)
        fixture(root / 'single.epub', 1, 2880000)
        for name, kind in [('book.txt', 'txt'), ('multi.epub', 'epub'), ('single.epub', 'epub')]:
            book = root / name
            seed = root / (name + '-seed')
            result, _ = run(args.after, kind, book, str(font), seed, root / (name + '-seed-frames'), 'seed')
            assert result['stored'] == 2, result
            records = {'before': [], 'after': []}
            expected = None
            for turn in range(3):
                for label in (('before', 'after') if turn % 2 == 0 else ('after', 'before')):
                    state = root / f'{name}-{turn}-{label}'
                    shutil.copytree(seed, state)
                    result, frames = run(getattr(args, label), kind, book, str(font), state, root / f'{name}-{turn}-{label}-frames')
                    assert result['hits'] == (2 if label == 'after' else 0), result
                    if expected is None:
                        expected = frames
                    assert expected == frames, (name, turn, label)
                    records[label].append(result)
            summary = {label: {key: statistics.median(r[key] for r in runs) for key in ('open_ns', 'next_ns', 'peak')} for label, runs in records.items()}
            report['books'][name] = {'median': summary, 'runs': records, 'cache_bytes': sum(p.stat().st_size for p in (seed / 'reading' / 'resume').glob('*.pnr')),
                                      'frame_sha': [hashlib.sha256(f).hexdigest() for f in expected]}
            print(name, json.dumps(summary), flush=True)
    pathlib.Path(args.out).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
