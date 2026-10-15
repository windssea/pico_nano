"""用户书籍在受限池内的开关书/翻页与清理诊断。 / Diagnose opening/turning/closing user books under constrained pools."""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-memory')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    books = [p for p in sorted((ROOT / 'tests/testBook').rglob('*')) if p.is_file() and p.suffix.lower() in ('.epub', '.txt')]
    hashes = {p: digest(p) for p in books}
    fonts = [p for p in sorted((ROOT / 'tests/testFont').rglob('*')) if p.is_file() and p.suffix.lower() == '.ttf']
    hashes.update({p: digest(p) for p in fonts})
    report = {'scope': 'Host application pool; excludes device SDK heap, stacks and panel', 'passed': False, 'runs': []}
    (args.output / 'report.json').write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        installed = root / 'fonts'
        installed.mkdir()
        for font in fonts:
            shutil.copy2(font, installed / font.name)
        for index, book in enumerate(books):
            library = root / f'books-{index}'
            library.mkdir()
            shutil.copy2(book, library / book.name)
            for kib, font in [(kib, None) for kib in (1024, 2048, 4096, 6144)] + [(6144, font) for font in fonts]:
                budget = kib * 1024
                label = f'{index}-{kib}-' + (font.stem if font else 'system')
                command = [str(args.sim.resolve()), '--library', str(library), '--budget', str(budget), '--state-dir', str(root / f'state-{label}')]
                if font:
                    command += ['--font', str(installed / font.name)]
                try:
                    session = subprocess.run(command,
                        env=dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_TIMING='1', PN_SIM_LIBRARY_SCRIPT='library,enter,next,previous,back,enter,window-close'), capture_output=True, text=True, timeout=180)
                except subprocess.TimeoutExpired as exc:
                    # 单场景超时仍保留输出并继续其他书/预算，不保留旧的绿色报告。/ Preserve timeout output and continue other books/budgets without leaving a stale green report.
                    decode = lambda value: value.decode('utf-8', 'replace') if isinstance(value, bytes) else value or ''
                    session = subprocess.CompletedProcess(command, 124, decode(exc.stdout), decode(exc.stderr))
                log = session.stdout + session.stderr
                (args.output / f'{label}.log').write_text(log, encoding='utf-8')
                peak = re.search(r'library_window pool_peak=(\d+) used=0 live=0', log)
                attempts = list(map(int, re.findall(r'library_open prepare .*? status=(\d+)', log)))
                ui = [int(re.search(r'status=(\d+)', line)[1]) for line in log.splitlines()
                      if line.startswith(('opening end ', 'library_turn ', 'library_return ')) and re.search(r'status=(\d+)', line)]
                opened = list(map(int, re.findall(r'opening end status=(\d+)', log)))
                # 低预算允许明确OOM退路，不能接受损坏误报、崩溃或退出泄漏。/ Constrained pools may report explicit OOM, never corruption, crashes or leaked allocations at exit.
                ok = session.returncode == 0 and bool(peak) and int(peak[1]) <= budget and bool(attempts) and all(s in (0, 5) for s in attempts) and all(s in (0, 3, 5, 8) for s in ui)
                entry = {'book': book.name, 'font': font.name if font else 'system', 'budget_bytes': budget, 'pool_peak_bytes': int(peak[1]) if peak else None,
                         'open_statuses': attempts, 'ui_statuses': ui, 'reading_opened': 0 in opened, 'controlled_oom': 5 in attempts or 5 in ui, 'cancelled': 8 in ui, 'passed': ok,
                         'phases': [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in log.splitlines() if line.startswith('profile phase=')],
                         'memory': [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in log.splitlines() if line.startswith('memory phase=')],
                         'cache': [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in log.splitlines() if line.startswith('cache phase=')]}
                report['runs'].append(entry)
                print(f'{"PASS" if ok else "FAIL"} {book.name} {kib}KiB open={attempts}', flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in hashes.items())
    report['reading_opened_runs'] = sum(run['reading_opened'] for run in report['runs'])
    report['controlled_oom_runs'] = sum(run['controlled_oom'] for run in report['runs'])
    report['default_budget_all_opened'] = all(run['reading_opened'] for run in report['runs'] if run['budget_bytes'] == 6144*1024)
    report['passed'] = bool(books) and report['originals_unchanged'] and report['default_budget_all_opened'] and all(run['passed'] for run in report['runs'])
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
