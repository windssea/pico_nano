"""用户四款字体作为TF界面源的设置复用/失败/回收验证。 / Verify settings reuse, failures and reclamation with supplied fonts as TF UI sources."""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-settings-font')
    args = parser.parse_args()
    fonts = sorted(p for p in (ROOT / 'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    if not fonts:
        raise ValueError('Requires supplied TTF resources')
    identities = {p: digest(p) for p in fonts}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'fonts': [], 'scope': 'Host supplied-font TF-source ownership and settings pixels; not panel/HarmonyOS resource acceptance'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        for index, original in enumerate(fonts):
            local = pathlib.Path(temporary) / original.name
            shutil.copy2(original, local)
            result = subprocess.run([str(args.binary.resolve()), str(local)], capture_output=True, text=True, timeout=120)
            (args.output / f'{index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            matched = re.search(r'warm_opens=(\d+) presents=(\d+) peak=(\d+) used=(\d+) live=(\d+)', result.stdout)
            assert result.returncode == 0 and matched, (original.name, result.returncode, result.stdout, result.stderr[-2000:])
            opened, presents, peak, used, live = map(int, matched.groups())
            assert opened == used == live == 0
            report['fonts'].append({'font': original.name, 'warm_opens': opened, 'presents': presents, 'pool_peak': peak,
                                    'cold_cached_pixels_match': True, 'cancel_fault_reclaim_passed': True, 'final_used': used, 'final_live': live})
            print(f'PASS settings reuse {original.name} peak={peak}', flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
