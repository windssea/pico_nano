"""A/B同进程换书，包含中断当前/下一页包与先存当前页；原创夹具。 / Same-process A/B switching with interrupted current/next writes and current-first storage, using original fixtures."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile
from test_resume import fixture
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from test_local_book_index import fixture as large_fixture

p = argparse.ArgumentParser()
p.add_argument('binary')
p.add_argument('--large', action='store_true')
args = p.parse_args()
records = {}
with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    if args.large:
        a, b = root / 'multi.epub', root / 'single.epub'
        large_fixture(a, 48, 12000)
        large_fixture(b, 1, 576000)
    else:
        fixture(root)
        a, b = root / 'book.epub', root / 'other.epub'
        b.write_bytes(a.read_bytes())
        with zipfile.ZipFile(b, 'a') as archive:
            archive.writestr('different-source.txt', 'Original second fixture identity')
    for mode in ('settled', 'interrupted', 'early', 'next-interrupted'):
        state, frames = root / ('state-' + mode), root / ('frames-' + mode)
        result = subprocess.run([args.binary, str(ROOT / 'assets/fonts/HarmonyOS_Sans_SC_Regular.ttf'), str(a), str(b), str(state), str(frames), mode], capture_output=True, text=True, timeout=150)
        assert result.returncode == 0, (mode, result.stdout, result.stderr)
        rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines() if line.startswith('switch ')]
        assert len(rows) == 4 and all(int(row['open_hits']) == 1 for row in rows[2:]), (mode, rows)
        print(result.stdout, end='', flush=True)
        records[mode] = {'rows': rows, 'frame_sha': {str(f.relative_to(frames)): hashlib.sha256(f.read_bytes()).hexdigest() for f in frames.glob('*/*.raw')}}
    destination = os.getenv('PN_SWITCH_EVIDENCE')
    if destination:
        path = pathlib.Path(destination)
        if not path.is_absolute():
            path = ROOT / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({'scope': 'host same-process shared core; original fixtures, no ESP32/TF/panel timing', 'large': args.large, 'cases': records}, indent=2) + '\n')
