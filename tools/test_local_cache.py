"""用户原书的跨进程续读、缓存命中与坏包恢复；仅改隔离副本。 / Cross-process restore, cache hits and corrupt-cache recovery using isolated user-book copies."""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest
from summarize_profile import summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=pathlib.Path, required=True)
    parser.add_argument('--book', help='Optional exact supplied filename for a focused reproduction')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-cache')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    books = [p for p in sorted((ROOT / 'tests/testBook').rglob('*')) if p.is_file() and p.suffix.lower() in ('.epub', '.txt')]
    if args.book:
        books = [p for p in books if p.name == args.book]
    hashes = {p: digest(p) for p in books}
    report = {'scope': 'Host/SDL cross-process persistent reading cache; OS file cache uncontrolled', 'books': []}
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for index, book in enumerate(books):
            library = root / f'books-{index}'
            state = root / f'state-{index}'
            library.mkdir()
            shutil.copy2(book, library / book.name)
            entry = {'book': book.name, 'runs': []}
            report['books'].append(entry)
            def run(label, script):
                frames = root / f'frames-{index}-{label}'
                frames.mkdir()
                result = subprocess.run([str(args.sim.resolve()), '--library', str(library), '--state-dir', str(state)],
                    env=dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_LIBRARY_SCRIPT='library,' + script + ',window-close', PN_SIM_FRAME_DIR=str(frames), PN_SIM_CLOCK='12:34', PN_SIM_TIMING='1'), capture_output=True, text=True, timeout=180)
                log = result.stdout + result.stderr
                (args.output / f'{index}-{label}.log').write_text(log, encoding='utf-8')
                captures = sorted(frames.glob('frame-*.pgm'))
                body = captures[-2].read_bytes() if len(captures) >= 2 else b''
                (args.output / f'{index}-{label}-reading.pgm').write_bytes(body)
                if captures:
                    shutil.copy2(captures[-1], args.output / f'{index}-{label}-last.pgm')
                stages = summarize(log)
                entry['runs'].append({'condition': label, 'exit': result.returncode, 'phases': stages,
                                      'pool_cleanup': 'used=0 live=0' in log,
                                      'positions': re.findall(r'(?:epub_)?library_position ([^\n]+)', result.stdout)})
                return result.returncode == 0 and 'used=0 live=0' in log and 'opening end status=0' in log, body, log
            try:
                cold, body, _ = run('cold', 'enter,jump,' + 'library-next,' * 5 + 'enter,next,previous,wait:3000')
                packets = list((state / 'semantic').glob('*.pnsm'))
                entry['semantic_packets'] = [{'name': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p)} for p in packets]
                page_packets = list(state.rglob('*.pni'))
                entry['page_index_packets'] = [{'name': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p)} for p in page_packets]
                warm, restored, log = run('warm', 'enter,wait:100')
                entry['warm_semantic_hit'] = bool(re.search(r'profile phase=epub.model-load .*?status=0', log))
                entry['full_source_sha_on_warm'] = 'profile phase=content.stream-sha ' in log
                entry['same_restored_pixels'] = bool(body) and body == restored
                entry['passed'] = cold and warm and entry['same_restored_pixels'] and entry['full_source_sha_on_warm']
                if packets or page_packets:
                    for packet in packets + page_packets:
                        data = bytearray(packet.read_bytes())
                        data[100] ^= 1
                        packet.write_bytes(data)
                    recovered, recovered_body, log = run('corrupt', 'enter,wait:100')
                    entry['corrupt_cache_recovery'] = recovered and recovered_body == body and (not packets or 'profile phase=epub.model-build ' in log)
                    entry['passed'] &= entry['corrupt_cache_recovery']
                else:
                    entry['cache_note'] = 'No eligible persistent packet was committed; recorded as a cache miss, not a hit.'
            except Exception as exc:
                entry['passed'] = False
                entry['error'] = str(exc)
            print(f'{"PASS" if entry["passed"] else "FAIL"} reading cache {book.name} hit={entry.get("warm_semantic_hit")} packets={len(entry.get("semantic_packets", []))}', flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in hashes.items())
    report['passed'] = bool(books) and report['originals_unchanged'] and all(book['passed'] for book in report['books'])
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
