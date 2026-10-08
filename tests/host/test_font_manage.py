"""字体管理页真实目录驱动。 / Font management page driven with real directories."""
import pathlib,shutil,subprocess,sys,tempfile
binary,root_dir=sys.argv[1],pathlib.Path(sys.argv[2])
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);fonts=root/'fonts';fonts.mkdir();state=root/'state';state.mkdir()
    shutil.copy(root_dir/'assets/fonts/read-pico-ui.ttf',fonts/'a-ui.ttf')
    shutil.copy(root_dir/'tests/fixtures/font-fallback.ttf',fonts/'b-fallback.ttf')
    (fonts/'c-bad.ttf').write_bytes(b'not a truetype font'*10)
    (fonts/'notes.txt').write_text('ignored')
    book=root/'book.txt';book.write_text('字体管理测试。\n'*50,encoding='utf-8')
    r=subprocess.run([binary,str(fonts),str(state),str(book),str(root/'detail.pgm')],capture_output=True,timeout=300)
    assert r.returncode==0,(r.stdout,r.stderr)
    assert (root/'detail.pgm').read_bytes().startswith(b'P5\n684 1216\n15\n')
    print(r.stdout.decode().strip())
print('font manage: list, bad font, detail, global default, delete cancel/busy/confirm, kept records, reading fallback, fault-injected open passed')
