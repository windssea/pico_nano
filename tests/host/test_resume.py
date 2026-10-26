"""跨进程续读包、完整像素和回退；耗时只是主机软件观察。 / Cross-process resume packets, exact pixels and fallbacks; timing is host software only."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile


def fixture(folder):
    text = '\n'.join(''.join(chr(0x4e00 + (i * 37 + j * 131) % 2048) for i in range(220)) for j in range(120))
    (folder / 'book.txt').write_text(text, encoding='utf-8')
    with zipfile.ZipFile(folder / 'book.epub', 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original resume fixture</d:title><d:identifier id="u">CC0-resume</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('a.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><body>' + ''.join('<p>' + p + '</p>' for p in text.splitlines()) + '</body></html>')


def run(exe, kind, book, font, state, output, mode='read', env=None):
    output.mkdir(parents=True, exist_ok=True)
    p = subprocess.run([exe, kind, str(book), font, str(state), str(output), mode], capture_output=True, text=True, timeout=120, env=env)
    assert p.returncode == 0, (p.args, p.stdout, p.stderr)
    data = json.loads(p.stdout.strip().splitlines()[-1])
    assert data['status'] == 0 and data['used'] == 0 and data['live'] == 0 and data['peak'] <= data['limit'] == 6 * 1024 * 1024, data
    return data, [p.read_bytes() for p in sorted(output.glob('*.raw'))]


def main():
    exe, font = sys.argv[1:3]
    baseline = sys.argv[3] if len(sys.argv) > 3 else exe
    with tempfile.TemporaryDirectory() as tmp:
        folder = pathlib.Path(tmp)
        # 使用同一字体字节的临时副本，排除Windows绑定卷的小读开销。/ Use an identical temporary font copy to avoid Windows bind-volume small-read overhead.
        copied_font = folder / 'font.ttf'
        shutil.copyfile(font, copied_font)
        font = str(copied_font)
        fixture(folder)
        for kind in ('txt', 'epub'):
            book = folder / ('book.' + kind)
            seed = folder / (kind + '-seed')
            data, _ = run(exe, kind, book, font, seed, folder / (kind + '-seed-frames'), 'seed')
            assert data['stored'] == 2, data
            reference = folder / (kind + '-reference')
            shutil.copytree(seed, reference)
            shutil.rmtree(reference / 'reading' / 'resume')
            cold, frames = run(baseline, kind, book, font, reference, folder / (kind + '-reference-frames'))
            switched = folder / (kind + '-switch')
            shutil.copytree(seed, switched)
            other = folder / ('other.' + kind)
            if kind == 'txt':
                other.write_text(book.read_text(encoding='utf-8') + '\n另一本原创书。', encoding='utf-8')
            else:
                with zipfile.ZipFile(book) as src, zipfile.ZipFile(other, 'w', compression=zipfile.ZIP_DEFLATED) as dst:
                    for name in src.namelist():
                        content = src.read(name)
                        if name == 'a.xhtml':
                            content = content.replace(b'</body>', '另一本原创书。</body>'.encode())
                        dst.writestr(name, content, compress_type=zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED)
            run(exe, kind, other, font, switched, folder / (kind + '-other-frames'), 'seed')
            result, actual = run(exe, kind, book, font, switched, folder / (kind + '-switch-frames'))
            # EPUB回翻可复用原当前包；TXT仍使用原RAM邻页。/ EPUB reverse turns can reuse the original current packet; TXT keeps its RAM neighbor path.
            reverse_hit = 1 if kind == 'epub' else 0
            assert result['hits'] == 2 + reverse_hit and actual == frames, (kind, 'switch', result)
            for case in ('warm', 'missing', 'next-missing', 'root-missing', 'corrupt', 'truncated', 'failed-display', 'clock'):
                state = folder / (kind + '-' + case)
                shutil.copytree(seed, state)
                packets = state / 'reading' / 'resume'
                if case == 'missing':
                    shutil.rmtree(packets)
                elif case == 'next-missing':
                    for p in packets.glob('*.1.pnr*'):
                        p.unlink()
                elif case == 'root-missing':
                    shutil.rmtree(state / 'resume')
                elif case in ('corrupt', 'truncated'):
                    for p in packets.glob('*.pnr*'):
                        data = bytearray(p.read_bytes())
                        if case == 'corrupt':
                            data[len(data) // 2] ^= 1
                        else:
                            del data[len(data) // 2:]
                        p.write_bytes(data)
                env = dict(os.environ, PN_RESUME_CLOCK='23:58') if case == 'clock' else None
                result, actual = run(exe, kind, book, font, state, folder / (kind + '-' + case + '-frames'), 'fail' if case == 'failed-display' else 'read', env)
                if case == 'clock':
                    reference2 = folder / (kind + '-clock-reference')
                    shutil.copytree(seed, reference2)
                    shutil.rmtree(reference2 / 'reading' / 'resume')
                    _, clock_frames = run(baseline, kind, book, font, reference2, folder / (kind + '-clock-reference-frames'), env=env)
                    assert actual == clock_frames, (kind, case, 'changed clock differs')
                else:
                    assert actual == frames, (kind, case, [hashlib.sha256(f).hexdigest() for f in actual], [hashlib.sha256(f).hexdigest() for f in frames])
                expected = 2 + reverse_hit if case in ('warm', 'clock') else 3 + reverse_hit if case == 'failed-display' else 1 + reverse_hit if case == 'next-missing' else 0
                assert result['hits'] == expected, (kind, case, result)
                print(kind, case, json.dumps(result), flush=True)
            for setting in ('style', 'visual'):
                state = folder / (kind + '-' + setting)
                shutil.copytree(seed, state)
                run(exe, kind, book, font, state, folder / (kind + '-' + setting + '-set'), setting)
                ref = folder / (kind + '-' + setting + '-reference')
                shutil.copytree(state, ref)
                shutil.rmtree(ref / 'reading' / 'resume')
                result, actual = run(exe, kind, book, font, state, folder / (kind + '-' + setting + '-new'))
                _, expected = run(baseline, kind, book, font, ref, folder / (kind + '-' + setting + '-old'))
                assert actual == expected and result['hits'] == 0, (kind, setting, result)
            print(kind, 'baseline', json.dumps(cold), 'settings fallback identical', flush=True)
            if kind == 'txt':
                original = book.read_bytes()
                timestamp = book.stat()
                changed = original[:-3] + ('海' if original[-3:] != '海'.encode() else '山').encode()
                assert len(changed) == len(original)
                book.write_bytes(changed)
                os.utime(book, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
                changed_frames = None
                for label, binary in [('before', baseline), ('after', exe)]:
                    state = folder / ('changed-' + label)
                    shutil.copytree(seed, state)
                    result, actual = run(binary, kind, book, font, state, folder / ('changed-' + label + '-frames'))
                    assert result['hits'] == 0, result
                    if changed_frames is None:
                        changed_frames = actual
                    assert actual == changed_frames
                book.write_bytes(original)
                print('same-size/same-mtime replacement rejects old resume identity', flush=True)
        image_book = folder / 'images.epub'
        with zipfile.ZipFile(folder / 'book.epub') as src, zipfile.ZipFile(image_book, 'w', compression=zipfile.ZIP_DEFLATED) as dst:
            for name in src.namelist():
                data = src.read(name)
                if name == 'a.xhtml':
                    data = b'<html xmlns="http://www.w3.org/1999/xhtml"><body><img src="picture.png"/>' + data.split(b'<body>')[1]
                elif name == 'book.opf':
                    data = data.replace(b'</manifest>', b'<item id="im" href="picture.png" media-type="image/png"/></manifest>')
                dst.writestr(name, data, compress_type=zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED)
            dst.writestr('picture.png', (pathlib.Path(__file__).resolve().parents[1] / 'fixtures' / 'image-black.png').read_bytes())
        state = folder / 'image-seed'
        result, _ = run(exe, 'epub', image_book, font, state, folder / 'image-seed-frames', 'seed-first')
        assert result['stored'] >= 1, result
        ref = folder / 'image-reference'
        shutil.copytree(state, ref)
        shutil.rmtree(ref / 'reading' / 'resume')
        _, expected = run(baseline, 'epub', image_book, font, ref, folder / 'image-reference-frames')
        result, actual = run(exe, 'epub', image_book, font, state, folder / 'image-warm-frames')
        assert actual == expected and result['hits'] >= 1, ('image', result)
        print('image resume/current/NEXT/previous full frames identical', json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
