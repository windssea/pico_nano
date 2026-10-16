"""可恢复ZIP的极端尾注/扩展区/哈希冲突边界；不是用户书籍体验用例。 / Resumable ZIP tail/extra/hash-collision boundaries, not book-experience fixtures."""
import io
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import warnings
import zipfile

validate, old, new, scan = sys.argv[1:5]
with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    def run(name, data, good=True):
        path = root / (name + '.zip')
        path.write_bytes(data)
        if good:
            for flags in ([],['--scan']):
                result = subprocess.run([validate, str(path),*flags], capture_output=True, timeout=180)
                assert result.returncode == 0 and b'used=0 live=0' in result.stdout, result.stdout + result.stderr
        else:
            statuses = []
            for binary in (old, new, scan):
                result = subprocess.run([binary, str(path), 'probe'], capture_output=True, timeout=30)
                assert result.returncode and b'used=0 live=0' in result.stderr, result.stderr
                statuses.append(re.search(rb'zip status=(\d+)', result.stderr)[1])
            assert len(set(statuses))==1, statuses
    def container(names=('probe',), extra=b'', comment=b''):
        out = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            with zipfile.ZipFile(out, 'w') as archive:
                for name in names:
                    info = zipfile.ZipInfo(name)
                    info.extra = extra
                    archive.writestr(info, b'bounded ZIP job input')
                archive.comment = comment
        return out.getvalue()
    run('empty', container(()))
    run('max-comment', container(comment=b'x' * 65535))
    # 两份中央扩展使目录超过128KiB，不依赖可选快照；空扩展字段需跨很多步。/ Two central extras exceed 128 KiB, bypassing the optional snapshot; empty fields span many steps.
    many_extra = b'\xfe\xca\0\0' * 16383
    data = container(('probe', 'second', 'third'), extra=many_extra)
    run('many-extra', data)
    bad = bytearray(data)
    struct.pack_into('<H', bad, 30 + len('probe'), 1)
    run('local-zip64-extra', bad, False)
    bad = bytearray(data)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        central = archive.start_dir
    struct.pack_into('<H', bad, central + 46 + len('probe'), 1)
    run('central-zip64-extra', bad, False)
    pair = ('collision-b626ed5a.txt', 'collision-a6063649.txt')
    def name_hash(name):
        value = 2166136261
        for byte in name.encode():
            value = ((value ^ byte) * 16777619) & 0xffffffff
        return value
    assert pair[0] != pair[1] and name_hash(pair[0]) == name_hash(pair[1]) == 0x110b36d3
    run('hash-collision', container(('probe', *pair)))
    run('duplicate-name', container(('probe', 'probe')), False)
print('ZIP job boundaries: empty/max-comment/large extras/ZIP64 rejection/hash collisions/duplicates passed')
