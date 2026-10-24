"""超大单章中断续建：完整边界/像素相同，防窗口准入失败退化。 / Resume an oversized chapter with exact boundaries/pixels and bounded work after cache admission pressure."""
import json
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='pn-index-large-') as directory:
    output = pathlib.Path(directory) / 'result'
    subprocess.run([sys.executable, str(root / 'tools/test_local_book_index.py'),
                    '--binary', sys.argv[1], '--font', sys.argv[2],
                    '--chapters', '1', '--chars', '1000000', '--resume-after', '1000',
                    '--output', str(output)], check=True, timeout=180)
    resumed = json.loads((output / 'resumed/complete.json').read_text())
    assert resumed['steps'] < 2000000, resumed
    print('Oversized chapter: complete/resumed exact table and first frame; bounded work and zero leaks')
