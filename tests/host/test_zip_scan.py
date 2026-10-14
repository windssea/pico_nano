"""原创ZIP样本：普通、数据描述符、超大本地扩展、错名、重叠，比较分步打开与整体打开。 / Original ZIP fixtures (plain, descriptors, oversized local extras, wrong names, overlap) comparing staged and whole opens."""
import io, pathlib, random, struct, subprocess, sys, tempfile, zipfile


class Unseekable(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()
    def writable(self):
        return True
    def write(self, b):
        self.data += b
        return len(b)


def plain(path, entries=60, seed=7):
    rng = random.Random(seed)
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for i in range(entries):
            z.writestr(f'OEBPS/c{i:03}.xhtml', ''.join(rng.choices('原创文本abcdef ', k=rng.randint(10, 9000))))


def descriptors(path):
    raw = Unseekable()
    with zipfile.ZipFile(raw, 'w', zipfile.ZIP_DEFLATED) as z:
        for i in range(12):
            with z.open(f'd{i}.txt', 'w') as f:
                f.write(('descriptor %d ' % i).encode() * (50 + i * 40))
    path.write_bytes(bytes(raw.data))


def big_extra(path):
    data = bytearray()
    with zipfile.ZipFile(io.BytesIO(), 'w') as _:
        pass
    plain(path, entries=5)
    blob = bytearray(path.read_bytes())
    # 重建：给第一个条目加512字节本地扩展（ID 0xCAFE），并修正其后偏移。/ Rebuild with a 512-byte local extra on the first entry and fix later offsets.
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        payloads = [(i, z.read(i)) for i in infos]
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, (info, content) in enumerate(payloads):
            new = zipfile.ZipInfo(info.filename, info.date_time)
            new.compress_type = zipfile.ZIP_DEFLATED
            if n == 0:
                new.extra = struct.pack('<HH', 0xCAFE, 508) + bytes(508)
            z.writestr(new, content)
    path.write_bytes(out.getvalue())


def wrong_name(path):
    plain(path, entries=8)
    blob = bytearray(path.read_bytes())
    at = blob.find(b'PK\x03\x04', 1)
    blob[at + 30] ^= 0x01  # 第二个本地头名称首字节 / first byte of the second local name
    path.write_bytes(bytes(blob))


def overlap(path):
    plain(path, entries=6)
    blob = bytearray(path.read_bytes())
    eocd = blob.rfind(b'PK\x05\x06')
    cd = struct.unpack_from('<I', blob, eocd + 16)[0]
    second = blob.find(b'PK\x01\x02', cd + 1)
    first_local = struct.unpack_from('<I', blob, cd + 42)[0]
    struct.pack_into('<I', blob, second + 42, first_local)
    path.write_bytes(bytes(blob))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        cases = [('plain', plain, 0), ('descriptors', descriptors, 0), ('extra', big_extra, 0), ('wrong-name', wrong_name, 1), ('overlap', overlap, 1)]
        args = [sys.argv[1]]
        for name, make, bad in cases:
            path = root / f'{name}.zip'
            make(path)
            args += [str(path), str(bad)]
        subprocess.run(args, check=True, timeout=120)
