"""SDL实际事件的书签编辑、跳转、取消与确认旅程。 / Bookmark editing, navigation, cancellation and confirmation through actual SDL events."""
import os
import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / "books"
    books.mkdir()
    (books / "book.txt").write_text("Read Pico book with editable bookmarks.\n" * 300, encoding="utf8")
    script = "enter,next,bookmarks,mark-add,mark-select,mark-rename,mark-clear,text:My mark,mark-confirm,tap:150:50,tap:620:500,tap:300:40,tap:200:200,mark-jump,mark-return,bookmarks,mark-select,mark-delete,mark-back,mark-delete,mark-confirm,mark-back,back,quit"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT=script)
    args = [sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")]
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "bookmark_ui command=4 status=0 mode=1 count=1" in result.stdout, result.stdout
    assert "bookmark_ui command=11 status=0 mode=1 count=1" in result.stdout, result.stdout
    assert "bookmark_label value=My mark" in result.stdout, result.stdout
    assert "library_turn pointer=1 status=0" in result.stdout, result.stdout
    assert "bookmark_ui command=6 status=0 mode=0 count=1" in result.stdout, result.stdout
    assert "bookmark_return status=0" in result.stdout, result.stdout
    assert "bookmark_ui command=1 status=0 mode=2 count=1" in result.stdout, result.stdout
    assert "bookmark_ui command=9 status=0 mode=1 count=0" in result.stdout, result.stdout
    assert "used=0 live=0" in result.stdout, result.stdout
    env["PN_SIM_LIBRARY_SCRIPT"] = "enter,bookmarks,mark-back,back,quit"
    resumed = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert resumed.returncode == 0 and "bookmark_ui open status=0 mode=1 count=0" in resumed.stdout, (resumed.stdout, resumed.stderr)
    reader_env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_INPUT_SCRIPT=script)
    reader = subprocess.run([sys.argv[1], "--book", str(books / "book.txt"), "--state-dir", str(root / "reader-state")], capture_output=True, text=True, env=reader_env, timeout=30)
    assert reader.returncode == 0 and "bookmark_label value=My mark" in reader.stdout and "reader_event action=pointer status=0" in reader.stdout and "bookmark_ui command=9 status=0 mode=1 count=0" in reader.stdout and "used=0 live=0" in reader.stdout, (reader.stdout, reader.stderr)
print("bookmark window: actual rename/jump/return/delete-cancel/delete-confirm/reopen events passed")

import zipfile
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory);books=root/'books';books.mkdir();book=books/'book.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>书签测试</d:title><d:identifier id="uid">bookmark-test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+'<p>书签记录原文位置，跳转后可以返回。修改名称，确认删除，继续阅读。</p>'*200+'</body></html>')
    for mode in ('--book','--library'):
        state=root/('single' if mode=='--book' else 'library');prefix='' if mode=='--book' else 'enter,';suffix='quit' if mode=='--book' else 'back,quit'
        def run(script):
            env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{('PN_SIM_INPUT_SCRIPT' if mode=='--book' else 'PN_SIM_LIBRARY_SCRIPT'):prefix+script+suffix})
            result=subprocess.run([sys.argv[1],mode,str(book if mode=='--book' else books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=40)
            assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
            return result
        r=run('tap:350:40,release:580:1160,mark-add,mark-select,mark-rename,mark-clear,text:第一章,mark-confirm,mark-back,next,bookmarks,mark-select,mark-jump,tap:430:40,')
        assert r.stdout.count('bookmark_ui command=4 status=0')==1 and 'bookmark_return status=0' in r.stdout,r.stdout
        assert 'bookmark_ui command=4 status=0 mode=1 count=1' in r.stdout and 'bookmark_label value=第一章' in r.stdout and 'bookmark_ui command=6 status=0 mode=0 count=1' in r.stdout,r.stdout
        r=run('bookmarks,mark-select,mark-delete,mark-back,mark-delete,mark-confirm,mark-back,')
        assert 'bookmark_ui command=1 status=0 mode=2 count=1' in r.stdout and 'bookmark_ui command=9 status=0 mode=1 count=0' in r.stdout,r.stdout
        r=run('bookmarks,mark-back,');assert 'bookmark_ui open status=0 mode=1 count=0' in r.stdout,r.stdout
print('EPUB bookmark windows: touch entry, ignored release, Chinese rename, jump/return, deletion cancellation/confirmation and reopen passed')
