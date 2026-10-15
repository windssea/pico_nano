"""只用用户EPUB核查可暂停元信息，损坏检查作用于副本。 / Verify resumable metadata with user EPUBs; corruption checks only touch copies."""
import argparse
import json
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-publication')
    args = parser.parse_args()
    books = sorted((ROOT / 'tests/testBook').rglob('*.epub'))
    if not books:
        raise ValueError('No user EPUBs')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'books': [], 'mutations': [], 'scope': 'Host shared source, no panel timing qualification'}
    report_path = args.output / 'report.json'
    report_path.write_text(json.dumps(report), encoding='utf-8')
    sizes = {}
    for book in books:
        with zipfile.ZipFile(book) as z:
            container = ET.fromstring(z.read('META-INF/container.xml'))
            package = next(e.attrib['full-path'] for e in container.iter() if e.tag.endswith('}rootfile'))
            sizes[book] = (z.getinfo(package).file_size, package)
    fault_book = min(books, key=lambda p: sizes[p][0])
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        def run(path, label, expected, faults=False):
            command = [str(args.binary.resolve()), str(path)] + (['--faults'] if faults else [])
            result = subprocess.run(command, capture_output=True, text=True, timeout=300)
            (args.output / f'{label}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            match = re.search(r'publication status=(\d+) steps=(\d+) allocations=(\d+) digest=(\w+) peak=(\d+) used=(\d+) live=(\d+)', result.stdout)
            assert result.returncode == 0 and match and int(match[1]) == expected and match[6] == match[7] == '0', label
            return {'status': int(match[1]), 'steps': int(match[2]), 'allocations': int(match[3]), 'digest': match[4], 'pool_peak_bytes': int(match[5]), 'faults': faults}
        for i, original in enumerate(books):
            identity = digest(original)
            path = root / original.name
            shutil.copy2(original, path)
            entry = run(path, str(i), 0, original == fault_book)
            entry.update(book=original.name, original_unchanged=digest(original) == identity)
            assert entry['original_unchanged']
            report['books'].append(entry)
            print(f'PASS publication {original.name} steps={entry["steps"]}', flush=True)
        package = sizes[fault_book][1]
        with zipfile.ZipFile(fault_book) as original:
            resources = [(info.filename, original.read(info)) for info in original.infolist()]
        for label, target, mutation, expected in (
                ('opf-crc', package, 'crc', 10), ('container-crc', 'META-INF/container.xml', 'crc', 10),
                ('opf-truncated', package, 'truncate', 10), ('opf-dtd', package, 'dtd', 9)):
            path = root / f'{label}.epub'
            with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as output:
                for name, data in resources:
                    if name == target and mutation == 'truncate':
                        data = data.rstrip()[:-5]
                    if name == target and mutation == 'dtd':
                        at = data.find(b'?>') + 2 if data.lstrip().startswith(b'<?xml') else 0
                        data = data[:at] + b'<!DOCTYPE package [<!ENTITY forbidden "x">]>' + data[at:]
                    output.writestr(name, data)
            if mutation == 'crc':
                data = bytearray(path.read_bytes())
                with zipfile.ZipFile(path) as z:
                    info = z.getinfo(target)
                    struct.pack_into('<I', data, info.header_offset + 14, info.CRC ^ 1)
                eocd = data.rfind(b'PK\x05\x06')
                at = struct.unpack_from('<I', data, eocd + 16)[0]
                while data[at:at + 4] == b'PK\x01\x02':
                    n, extra, comment = struct.unpack_from('<HHH', data, at + 28)
                    name = data[at + 46:at + 46 + n].decode('utf-8')
                    if name == target:
                        crc = struct.unpack_from('<I', data, at + 16)[0]
                        struct.pack_into('<I', data, at + 16, crc ^ 1)
                        break
                    at += 46 + n + extra + comment
                else:
                    raise AssertionError('Missing central record')
                path.write_bytes(data)
            entry = run(path, label, expected)
            entry['name'] = label
            report['mutations'].append(entry)
    report['passed'] = True
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
