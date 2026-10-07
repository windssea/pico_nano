"""排版草稿预览/取消/应用与重启。 / Typesetting draft preview/cancel/apply and restart."""
import os
import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / "books"
    books.mkdir()
    (books / "book.txt").write_text("Read Pico typesetting changes preserve the anchor.\n" * 300, encoding="utf8")
    args = [sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")]
    script = "enter,next,styles,style-plus,style-preview,style-form,style-cancel,styles,style-plus,style-down,style-down,style-down,style-down,style-plus,style-apply,back,quit"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT=script)
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "style_ui command=1 status=0" in result.stdout and "style_ui command=3 status=0 active=0" in result.stdout and "style_ui command=2 status=0 active=0" in result.stdout, result.stdout
    env["PN_SIM_LIBRARY_SCRIPT"] = "continue,styles,style-cancel,back,quit"
    reopened = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert reopened.returncode == 0 and "style_ui open status=0 pixels=34" in reopened.stdout and "used=0 live=0" in reopened.stdout, (reopened.stdout, reopened.stderr)
    assert "tracking=5" in result.stdout, result.stdout
    # 从真实保存载荷检查第七项，避免仅凭运行日志判断恢复。/ Check the actual saved payload for the seventh field rather than inferring restore from logs.
    import struct
    snapshots=list((root / 'state').glob('*.style.*'))
    assert snapshots
    assert any(b'PNTS' in p.read_bytes() and struct.unpack_from('<H',p.read_bytes(),p.read_bytes().index(b'PNTS')+52)[0]==5 for p in snapshots)
    standalone_env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_INPUT_SCRIPT="styles,style-plus,style-preview,style-form,style-cancel,styles,style-plus,style-apply,quit")
    single = subprocess.run([sys.argv[1], "--book", str(books / "book.txt"), "--state-dir", str(root / "single")], capture_output=True, text=True, env=standalone_env, timeout=30)
    assert single.returncode == 0 and "style_ui command=2 status=0 active=0" in single.stdout and "used=0 live=0" in single.stdout, (single.stdout, single.stderr)
print("style window: real preview/cancel/apply and durable reopen passed")

# EPUB与TXT复用同一中文表单，检查真实磁盘载荷。/ EPUB and TXT share the Chinese form; inspect actual disk payloads.
import zipfile
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory); books=root/'books'; books.mkdir(); book=books/'book.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>排版设置测试</d:title><d:identifier id="uid">style-test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+ '<p>首行缩进，行间距，段落间距，字间距。阅读设置预览和取消。</p>'*150 +'</body></html>')
    for mode in ('--book','--library'):
        state=root/('single' if mode=='--book' else 'library')
        script_var='PN_SIM_INPUT_SCRIPT' if mode=='--book' else 'PN_SIM_LIBRARY_SCRIPT'
        prefix='' if mode=='--book' else 'enter,'
        suffix='quit' if mode=='--book' else 'back,quit'
        def run(script):
            env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{script_var:prefix+script+suffix})
            result=subprocess.run([sys.argv[1],mode,str(book if mode=='--book' else books),'--state-dir',str(state)],capture_output=True,text=True,env=env,timeout=40)
            assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
            return result
        run('tap:520:40,style-plus,style-preview,style-form,style-cancel,')
        assert not list(state.glob('*.style.*'))
        result=run('styles,style-plus,style-down,style-down,style-down,style-plus,style-down,style-plus,style-preview,style-form,style-apply,')
        assert 'style_ui command=2 status=0 active=0' in result.stdout,result.stdout
        snapshots=list(state.glob('*.style.*')); assert snapshots
        payload=snapshots[0].read_bytes(); at=payload.index(b'PNTS')
        assert struct.unpack_from('<H',payload,at+40)[0]==(46 if mode=='--book' else 34) and struct.unpack_from('<H',payload,at+46)[0]==1 and struct.unpack_from('<H',payload,at+52)[0]==5
        before={p.name:p.read_bytes() for p in snapshots}
        run('styles,style-plus,style-preview,window-close,')
        assert {p.name:p.read_bytes() for p in state.glob('*.style.*')}==before
print('EPUB styles: standalone/library pointer entry, draft preview/cancel/apply, durable fields and preview-close passed')
