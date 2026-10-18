"""点击同一本正在预计算身份的书：异步接续后快开，加载门和退出池正常。/ Clicking a book already being identity-precomputed: asynchronous takeover then fast open, with the opening gate and pool released."""
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

root = pathlib.Path('/sdcard/piconano/books')
state = pathlib.Path('/data/progress')
for folder in (root, state):
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
book = root / 'takeover.epub'
with zipfile.ZipFile(book, 'w') as z:
    z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    z.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="id"><metadata><dc:identifier id="id">takeover</dc:identifier><dc:title>Takeover</dc:title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
    z.writestr('OPS/body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Takeover</title></head><body>' + ('<p>identity takeover example 0123456789</p>' * 80) + '</body></html>')
env = dict(os.environ, PN_TAKEOVER_BOOK=str(book), PN_OPEN_PROFILE='1')
result = subprocess.run([sys.argv[1]], env=env, capture_output=True, text=True, timeout=120)
assert result.returncode == 0, (result.stdout, result.stderr)
assert 'takeover_probe polls=' in result.stdout and 'menu_probe closed_pool_zero=1' in result.stdout, (result.stdout, result.stderr)
print(next(line for line in result.stdout.splitlines() if line.startswith('takeover_probe')))
for phase in ('begin','directory','zip-finish','publication','metadata','state','fonts','sources','trusted-wall','max-step','wait','first-frame','request'):
    assert 'profile phase=epub.open.'+phase+' ' in result.stdout, phase
for phase in ('storage.file-read','storage.file-seek','content.sha-read','content.sha-update'):
    assert 'profile phase='+phase+' ' in result.stdout, phase
# 预计算顺序：最近阅读优先于当前选择；测试结束清理状态。
# Precompute priority: last-read before selected; clean state afterwards.
for name in ('aaa.txt', 'bbb.txt', 'ccc.txt'):
    (root / name).write_text('priority order example\n' * 200, encoding='utf-8')
priority = subprocess.run([sys.argv[1]], env=dict(os.environ, PN_IDENTITY_PRIORITY='1'), capture_output=True, text=True, timeout=120)
assert priority.returncode == 0 and 'priority_probe ok' in priority.stdout, (priority.stdout, priority.stderr)
print('priority_probe ok')
# 不给同一容器里的后续启动用例留下书籍/进度状态。/ Leave no book/progress state for later boot cases in the same container.
shutil.rmtree(root)
shutil.rmtree(state)
