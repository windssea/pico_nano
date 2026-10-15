"""用用户EPUB副本核查mimetype描述符兼容和损坏拒绝。 / Check mimetype descriptor compatibility and corruption rejection using user EPUB copies."""
import argparse
import io
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import zipfile
import zlib
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-mimetype')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    with tempfile.TemporaryDirectory() as temporary:
        target = pathlib.Path(temporary) / 'book.epub'
        for book in sorted((ROOT / 'tests/testBook').rglob('*.epub')):
            identity = digest(book)
            with zipfile.ZipFile(book) as archive:
                info = archive.getinfo('mimetype')
                if not info.flag_bits & 8 and info.compress_type == 0:
                    continue
            raw = bytearray(book.read_bytes())
            names, extras = struct.unpack_from('<HH', raw, info.header_offset + 26)
            begin = info.header_offset + 30 + names + extras
            if info.compress_type != 0:
                damaged = bytearray(raw)
                damaged[begin + 1] ^= 1
                replacement = io.BytesIO()
                with zipfile.ZipFile(book) as source, zipfile.ZipFile(replacement, 'w') as copy:
                    for member in source.infolist():
                        copy.writestr(member, b'application/epub+ziX' if member.filename == 'mimetype' else source.read(member.filename))
                cases = {'original': (raw, 0), 'bad-compressed-content': (damaged, 10), 'wrong-type-valid-crc': (replacement.getvalue(), 10)}
                entry = {'book': book.name, 'sha256': identity, 'cases': []}
                for name, (data, expected) in cases.items():
                    target.write_bytes(data)
                    result = subprocess.run([str(args.binary.resolve()), str(target)], capture_output=True, timeout=60)
                    text = result.stderr.decode(errors='replace')
                    status = re.search(r'epub status=(\d+)', text)
                    ok = bool(status and int(status[1]) == expected and 'used=0 live=0' in text and (bool(result.stdout) if expected == 0 else not result.stdout))
                    entry['cases'].append({'name': name, 'passed': ok, 'status': int(status[1]) if status else None})
                    (args.output / f'{len(report)}-{name}.log').write_bytes(result.stdout + result.stderr)
                entry['original_unchanged'] = digest(book) == identity
                report.append(entry)
                print(f'{book.name}: {entry["cases"]}', flush=True)
                continue
            descriptor = begin + info.compress_size
            crc_at = descriptor + (4 if raw[descriptor:descriptor + 4] == b'PK\x07\x08' else 0)
            central = raw.rfind(b'PK\x05\x06')
            central = struct.unpack_from('<I', raw, central + 16)[0]
            while raw[central:central + 4] == b'PK\x01\x02':
                name_size, extra_size, comment_size = struct.unpack_from('<HHH', raw, central + 28)
                if raw[central + 46:central + 46 + name_size] == b'mimetype':
                    break
                central += 46 + name_size + extra_size + comment_size
            cases = {'original': (raw, 0)}
            corrupt_descriptor = bytearray(raw)
            corrupt_descriptor[crc_at] ^= 1
            cases['bad-descriptor'] = (corrupt_descriptor, 10)
            corrupt_content = bytearray(raw)
            corrupt_content[begin] ^= 1
            cases['bad-content-crc'] = (corrupt_content, 10)
            wrong_type = bytearray(raw)
            wrong_type[begin + 19] = ord('X')
            crc = zlib.crc32(wrong_type[begin:begin + 20])
            struct.pack_into('<I', wrong_type, crc_at, crc)
            struct.pack_into('<I', wrong_type, central + 16, crc)
            # 描述符模式的本地CRC可为零；非零时也保持一致，隔离实际类型判断。/ Descriptor-mode local CRC may be zero; update nonzero values to isolate MIME content validation.
            if struct.unpack_from('<I', wrong_type, info.header_offset + 14)[0]:
                struct.pack_into('<I', wrong_type, info.header_offset + 14, crc)
            cases['wrong-type-valid-crc'] = (wrong_type, 10)
            entry = {'book': book.name, 'sha256': identity, 'cases': []}
            for name, (data, expected) in cases.items():
                target.write_bytes(data)
                result = subprocess.run([str(args.binary.resolve()), str(target)], capture_output=True, timeout=60)
                text = result.stderr.decode(errors='replace')
                status = re.search(r'epub status=(\d+)', text)
                ok = bool(status and int(status[1]) == expected and 'used=0 live=0' in text and (bool(result.stdout) if expected == 0 else not result.stdout))
                entry['cases'].append({'name': name, 'passed': ok, 'status': int(status[1]) if status else None})
                (args.output / f'{len(report)}-{name}.log').write_bytes(result.stdout + result.stderr)
            entry['original_unchanged'] = digest(book) == identity
            report.append(entry)
            print(f'{book.name}: {entry["cases"]}', flush=True)
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(any(not e['original_unchanged'] or not all(c['passed'] for c in e['cases']) for e in report))


if __name__ == '__main__':
    raise SystemExit(main())
