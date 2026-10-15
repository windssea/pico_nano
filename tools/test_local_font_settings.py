"""用用户原书/字体验证全局默认、逐书覆盖与继承；只操作副本。 / Validate global defaults, per-book overrides and inheritance using copies of user books/fonts."""
import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def latest(state, pattern):
    """读取最新双槽记录用于独立核查。 / Read the newest journal slot for independent checks."""
    paths = list(state.glob(pattern))
    if not paths:
        return None
    payload = max((path.read_bytes() for path in paths), key=lambda data: struct.unpack_from('<Q', data, 8)[0])
    at = payload.index(b'PNFP')
    return {'inherit': bool(struct.unpack_from('<H', payload, at + 6)[0] & 2),
            'primary_kind': struct.unpack_from('<H', payload, at + 40)[0],
            'path': payload[at + 136:at + 136 + struct.unpack_from('<H', payload, at + 42)[0]].decode('utf-8')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-font-settings')
    args = parser.parse_args()
    originals = [p for directory in ('testBook', 'testFont') for p in sorted((ROOT / 'tests' / directory).rglob('*')) if p.is_file()]
    hashes = {p: digest(p) for p in originals}
    books = [p for p in originals if p.suffix.lower() in ('.epub', '.txt')]
    fonts = [p for p in originals if p.suffix.lower() == '.ttf']
    if not books or len(fonts) < 2:
        raise ValueError('Requires user books and at least two user fonts')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'scope': 'Host/SDL; no panel qualification', 'books': [],
              'protected_system_font': 'not exercised: supplied fonts do not include the canonical system fallback'}
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        installed = root / 'fonts'
        installed.mkdir()
        # 副本加数字前缀固定目录次序，不把Python码点排序误当产品拼音排序。/ Prefix copies to fix row order instead of confusing Python codepoint order with product pinyin order.
        installed_fonts = [installed / f'{fi:02d}-{font.name}' for fi, font in enumerate(fonts)]
        for font, target in zip(fonts, installed_fonts):
            shutil.copy2(font, target)
        for index, book in enumerate(books):
            library = root / f'books-{index}'
            state = root / f'state-{index}'
            library.mkdir()
            state.mkdir()
            shutil.copy2(book, library / book.name)
            entry = {'book': book.name, 'checks': [], 'passed': True}
            report['books'].append(entry)
            def run(label, script):
                result = subprocess.run([str(args.sim.resolve()), '--library', str(library), '--font-dir', str(installed), '--state-dir', str(state)],
                    env=dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_LIBRARY_SCRIPT='library,' + script + ',window-close'), capture_output=True, text=True, timeout=180)
                (args.output / f'{index}-{label}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                return result.returncode == 0 and 'used=0 live=0' in result.stdout, result.stdout
            def check(label, condition):
                entry['checks'].append({'name': label, 'passed': bool(condition)})
                entry['passed'] &= bool(condition)
            try:
                ok, log = run('global', 'fonts,tap:100:380,tap:100:1000,back,back,enter,back')
                check('management_global_default', ok and 'font_manage command=4 status=0' in log and latest(state, 'fonts.*')['path'] == str(installed_fonts[0]) and latest(state, '*.fonts.*') is None)
                for fi, font in enumerate(fonts):
                    ok, log = run(f'override-{fi}', f'enter,styles,fonts,tap:100:{430 + 88 * fi},style-cancel,back')
                    saved = latest(state, '*.fonts.*')
                    check(f'per_book_font_{fi}', ok and saved and not saved['inherit'] and saved['path'] == str(installed_fonts[fi]) and 'font_ui command=' in log)
                before = latest(state, '*.fonts.*')
                ok, log = run('global-change', 'fonts,tap:100:480,tap:100:1000,back,back,enter,styles,fonts,font-cancel,style-cancel,back')
                check('global_change_preserves_book_override', ok and latest(state, '*.fonts.*') == before and latest(state, 'fonts.*')['path'] == str(installed_fonts[1]))
                ok, log = run('inherit', 'enter,styles,fonts,font-inherit,style-cancel,back,enter,back')
                saved = latest(state, '*.fonts.*')
                check('restore_inheritance_and_reopen', ok and saved and saved['inherit'] and saved['primary_kind'] == 0 and 'font_ui command=13 status=0' in log)
                before = latest(state, '*.fonts.*')
                ok, log = run('cancel', 'enter,styles,fonts,font-select,font-preview,font-form,font-back,font-cancel,style-cancel,back')
                check('preview_cancel_preserves_selection', ok and latest(state, '*.fonts.*') == before)
                ok, log = run('resident', 'enter,styles,fonts,tap:100:270,font-apply,style-cancel,back')
                saved = latest(state, '*.fonts.*')
                check('explicit_resident_font', ok and saved and not saved['inherit'] and saved['primary_kind'] == 0)
            except Exception as exc:
                entry['passed'] = False
                entry['error'] = str(exc)
            print(f'{"PASS" if entry["passed"] else "FAIL"} font settings {book.name}', flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in hashes.items())
    report['passed'] = report['originals_unchanged'] and all(book['passed'] for book in report['books'])
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
