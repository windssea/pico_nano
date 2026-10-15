"""用户书/字体副本驱动真实产品菜单的文件与字形计数。 / File/glyph counters on real product menus using supplied book/font copies."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-menu-probe')
    parser.add_argument('--baseline', type=pathlib.Path, help='Compare every full packed frame with an earlier probe output')
    parser.add_argument('--cancel-system', action='store_true', help='Observe a cancelled system-font readiness check and its retry')
    args = parser.parse_args()
    if not pathlib.Path('/.dockerenv').is_file():
        raise ValueError('Requires a fresh disposable Docker container')
    library = pathlib.Path('/sdcard/piconano/books')
    state = pathlib.Path('/data/progress')
    font = pathlib.Path('/sdcard/piconano/fonts/HarmonyOS_Sans_SC_Regular.ttf')
    assert not library.exists() and not state.exists() and not font.exists()
    library.mkdir(parents=True)
    state.mkdir(parents=True)
    font.parent.mkdir(parents=True)
    books = sorted(p for p in (ROOT / 'tests/testBook').rglob('*') if p.is_file())
    fonts = sorted(p for p in (ROOT / 'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    assert books and fonts and len({p.name for p in books}) == len(books)
    originals = {p: digest(p) for p in books + fonts}
    for original in books:
        shutil.copy2(original, library / original.name)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'scope': 'Actual app_main menus with hardware substitutes; supplied fonts each serve as the fixed TF UI source. Host elapsed time is not TF/panel latency; recent previews are seeded, not reading acceptance.', 'cancelled_system_probe':args.cancel_system, 'books': [p.name for p in books], 'fonts': []}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf8')
    for index, original in enumerate(fonts):
        shutil.copy2(original, font)
        frames = args.output.resolve() / str(index)
        frames.mkdir(exist_ok=True)
        environment={**os.environ, 'PN_MENU_FRAMES': str(frames)}
        environment.pop('PN_MENU_CANCEL_SYSTEM_PROBE', None)
        if args.cancel_system:environment['PN_MENU_CANCEL_SYSTEM_PROBE']='1'
        result = subprocess.run([str(args.binary.resolve())], capture_output=True, text=True, timeout=180, env=environment)
        (args.output / f'{index}.log').write_text(result.stdout + result.stderr, encoding='utf8')
        assert result.returncode == 0 and 'menu_probe closed_pool_zero=1' in result.stdout, (original.name, result.returncode, result.stdout[-2500:], result.stderr[-2500:])
        assert 'index_rebuild retained_system_face=1 font_bytes=0 font_opens=0' in result.stdout
        rows = []
        for line in result.stdout.splitlines():
            if not line.startswith('menu name='):
                continue
            row = dict(word.split('=', 1) for word in line.split()[1:])
            rows.append({key: value if key == 'name' else int(value) for key, value in row.items()})
        assert len(rows) == 17
        captured = sorted(frames.glob('*.raw'))
        assert len(captured) == 17 and all(p.stat().st_size == 415872 for p in captured)
        if args.baseline:
            before = args.baseline / str(index)
            assert {p.name for p in captured} == {p.name for p in before.glob('*.raw')}
            assert all(p.read_bytes() == (before / p.name).read_bytes() for p in captured)
        extra={}
        if args.cancel_system:
            for label in ('system_cancel','system_retry'):
                lines=[line for line in result.stdout.splitlines() if line.startswith(label+' ')];assert len(lines)==1
                extra[label]={k:int(v) for k,v in (word.split('=',1) for word in lines[0].split()[1:])}
            assert extra['system_cancel']['retained_source']==extra['system_cancel']['retained_ui']==1
            assert extra['system_cancel']['used_before']==extra['system_cancel']['used_after'] and extra['system_cancel']['leases_before']==extra['system_cancel']['leases_after']
            assert all(value==0 for value in extra['system_retry'].values())
        report['fonts'].append({'font': original.name, 'index_retained_face': True, 'index_font_bytes': 0, 'menus': rows, 'closed_pool_zero': True, 'frames': {p.name: digest(p) for p in captured}, 'baseline_frames_match': True if args.baseline else None, **extra})
        print('PASS menu probe', original.name, flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in originals.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
